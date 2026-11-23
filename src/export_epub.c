/**
 * @file export_epub.c
 * @brief Native EPUB 2 and EPUB 3 e-book exporter using libzip.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "epub.h"
#include "export.h"
#include "export_internal.h"
#include "markdown.h"
#include "utf8.h"

#include <zip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdarg.h>

#define EPUB_MAX_CHAPTERS 256
#define EPUB_MAX_IMAGES   128
#define EPUB_MAX_SUBHEADS 64
#define EPUB_MAX_FOOTNOTES 128

/* =========================================================================
 * Dynamic String Buffer Utility
 * ========================================================================= */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} EpubStrBuf;

static void strbuf_init(EpubStrBuf *sb) {
    sb->cap = 1024;
    sb->len = 0;
    sb->data = (char *)malloc(sb->cap);
    if (sb->data) sb->data[0] = '\0';
}

static void strbuf_free(EpubStrBuf *sb) {
    if (sb->data) {
        free(sb->data);
        sb->data = NULL;
    }
    sb->len = 0;
    sb->cap = 0;
}

static bool strbuf_ensure(EpubStrBuf *sb, size_t extra) {
    if (!sb || !sb->data) return false;
    if (extra > SIZE_MAX - sb->len - 1) return false;
    size_t need = sb->len + extra + 1;
    if (need > sb->cap) {
        size_t min_grow = (need <= SIZE_MAX - 1024) ? (need + 1024) : need;
        size_t doubled = (sb->cap <= SIZE_MAX / 2) ? (sb->cap * 2) : SIZE_MAX;
        size_t new_cap = (doubled > min_grow) ? doubled : min_grow;
        char *n = (char *)realloc(sb->data, new_cap);
        if (!n) return false;
        sb->data = n;
        sb->cap = new_cap;
    }
    return true;
}

static void strbuf_puts(EpubStrBuf *sb, const char *s) {
    if (!s) return;
    size_t slen = strlen(s);
    if (!strbuf_ensure(sb, slen)) return;
    memcpy(sb->data + sb->len, s, slen);
    sb->len += slen;
    sb->data[sb->len] = '\0';
}

static void strbuf_putc(EpubStrBuf *sb, char c) {
    if (!strbuf_ensure(sb, 1)) return;
    sb->data[sb->len++] = c;
    sb->data[sb->len] = '\0';
}

static void strbuf_printf(EpubStrBuf *sb, const char *fmt, ...) {
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);

    if (n < 0) return;
    if ((size_t)n < sizeof(tmp)) {
        strbuf_puts(sb, tmp);
    } else {
        if (!strbuf_ensure(sb, (size_t)n)) return;
        va_start(ap, fmt);
        int written = vsnprintf(sb->data + sb->len, sb->cap - sb->len, fmt, ap);
        va_end(ap);
        if (written > 0 && (size_t)written < sb->cap - sb->len) {
            sb->len += (size_t)written;
        }
        sb->data[sb->len] = '\0';
    }
}

static void strbuf_xml_escape(EpubStrBuf *sb, const char *s, size_t len) {
    if (!s) return;
    for (size_t i = 0; i < len; ++i) {
        switch (s[i]) {
            case '&':  strbuf_puts(sb, "&amp;");  break;
            case '<':  strbuf_puts(sb, "&lt;");   break;
            case '>':  strbuf_puts(sb, "&gt;");   break;
            case '"':  strbuf_puts(sb, "&quot;"); break;
            case '\'': strbuf_puts(sb, "&apos;"); break;
            default:   strbuf_putc(sb, s[i]);     break;
        }
    }
}

/* =========================================================================
 * Internal EPUB Data Structures
 * ========================================================================= */

typedef struct {
    char title[128];
    char slug[128];
    int level;
} EpubSubhead;

typedef struct {
    char id[64];
    char label[64];
    char *text_xhtml;
} EpubFootnote;

typedef struct {
    char id[64];                  /**< e.g., "ch001" */
    char filename[64];            /**< e.g., "ch001.xhtml" */
    char title[256];              /**< Chapter title for TOC */
    char doc_anchor_id[64];       /**< Originating document anchor ID */
    EpubStrBuf body;              /**< XHTML body markup */
    EpubSubhead subheads[EPUB_MAX_SUBHEADS];
    size_t subhead_count;
    EpubFootnote footnotes[EPUB_MAX_FOOTNOTES];
    size_t footnote_count;
} EpubInternalChapter;

typedef struct {
    char id[64];
    char filename[128];
    char disk_path[PATH_MAX];
    char media_type[64];
    bool is_cover;
} EpubBundledImage;

typedef struct {
    EpubVersion version;
    EpubMetadata meta;
    EpubInternalChapter chapters[EPUB_MAX_CHAPTERS];
    size_t chapter_count;
    EpubBundledImage images[EPUB_MAX_IMAGES];
    size_t image_count;
    int current_play_order;
} EpubBookContext;

/* =========================================================================
 * Public API Implementations
 * ========================================================================= */

void epub_options_init(EpubOptions *opts) {
    if (!opts) return;
    memset(opts, 0, sizeof(*opts));
    opts->version = EPUB_VERSION_3;
    opts->split_h1 = true;
    opts->include_linked = true;
    opts->progress_cb = NULL;
    opts->user_data = NULL;
}

/** @brief Trim whitespace in-place from both ends of a null-terminated string. */
static void trim_whitespace(char *str) {
    if (!str) return;
    char *p = str;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != str) memmove(str, p, strlen(p) + 1);
    size_t l = strlen(str);
    while (l > 0 && isspace((unsigned char)str[l - 1])) {
        str[--l] = '\0';
    }
}

/** @brief Strip optional single or double quotes surrounding a string. */
static void unwrap_quotes(char *str) {
    if (!str) return;
    trim_whitespace(str);
    size_t l = strlen(str);
    if (l >= 2 && ((str[0] == '"' && str[l - 1] == '"') ||
                   (str[0] == '\'' && str[l - 1] == '\''))) {
        memmove(str, str + 1, l - 2);
        str[l - 2] = '\0';
        trim_whitespace(str);
    }
}

bool epub_parse_front_matter(const Buffer *buf, EpubMetadata *meta, size_t *out_body_start_line) {
    if (out_body_start_line) *out_body_start_line = 0;
    if (!buf || buffer_line_count(buf) == 0 || !meta) return false;

    const char *first_line = buffer_line(buf, 0);
    size_t first_len = buffer_line_len(buf, 0);

    // Check if line 0 is opening "---"
    size_t start = 0;
    while (start < first_len && isspace((unsigned char)first_line[start])) start++;
    if (first_len - start < 3 || strncmp(first_line + start, "---", 3) != 0) {
        return false;
    }
    // Remainder of line must be whitespace
    for (size_t i = start + 3; i < first_len; ++i) {
        if (!isspace((unsigned char)first_line[i])) return false;
    }

    size_t total_lines = buffer_line_count(buf);
    size_t end_line = 0;
    bool found_closing = false;

    for (size_t r = 1; r < total_lines; ++r) {
        const char *l = buffer_line(buf, r);
        size_t len = buffer_line_len(buf, r);

        size_t s = 0;
        while (s < len && isspace((unsigned char)l[s])) s++;

        if (len - s >= 3 && (strncmp(l + s, "---", 3) == 0 || strncmp(l + s, "...", 3) == 0)) {
            bool only_dashes = true;
            for (size_t i = s + 3; i < len; ++i) {
                if (!isspace((unsigned char)l[i])) { only_dashes = false; break; }
            }
            if (only_dashes) {
                end_line = r;
                found_closing = true;
                break;
            }
        }

        const char *colon = (const char *)memchr(l, ':', len);
        if (colon) {
            size_t raw_klen = (size_t)(colon - l);
            size_t klen = raw_klen;
            char key[64];
            if (klen >= sizeof(key)) klen = sizeof(key) - 1;
            memcpy(key, l, klen);
            key[klen] = '\0';
            trim_whitespace(key);
            for (char *p = key; *p; ++p) *p = (char)tolower((unsigned char)*p);

            size_t vlen = len - (raw_klen + 1);
            char val[512];
            if (vlen >= sizeof(val)) vlen = sizeof(val) - 1;
            memcpy(val, colon + 1, vlen);
            val[vlen] = '\0';
            unwrap_quotes(val);

            if (strcmp(key, "title") == 0) {
                snprintf(meta->title, sizeof(meta->title), "%s", val);
            } else if (strcmp(key, "author") == 0 || strcmp(key, "creator") == 0) {
                snprintf(meta->author, sizeof(meta->author), "%s", val);
            } else if (strcmp(key, "language") == 0 || strcmp(key, "lang") == 0) {
                snprintf(meta->language, sizeof(meta->language), "%s", val);
            } else if (strcmp(key, "date") == 0) {
                snprintf(meta->date, sizeof(meta->date), "%s", val);
            } else if (strcmp(key, "publisher") == 0) {
                snprintf(meta->publisher, sizeof(meta->publisher), "%s", val);
            } else if (strcmp(key, "identifier") == 0 || strcmp(key, "id") == 0 ||
                       strcmp(key, "isbn") == 0 || strcmp(key, "uuid") == 0) {
                snprintf(meta->identifier, sizeof(meta->identifier), "%s", val);
            } else if (strcmp(key, "rights") == 0 || strcmp(key, "copyright") == 0) {
                snprintf(meta->rights, sizeof(meta->rights), "%s", val);
            } else if (strcmp(key, "description") == 0 || strcmp(key, "summary") == 0) {
                snprintf(meta->description, sizeof(meta->description), "%s", val);
            } else if (strcmp(key, "cover") == 0 ||
                       strcmp(key, "cover-image") == 0 ||
                       strcmp(key, "cover_image") == 0 ||
                       strcmp(key, "coverimage") == 0 ||
                       strcmp(key, "cover-picture") == 0 ||
                       strcmp(key, "cover_picture") == 0 ||
                       strcmp(key, "coverpicture") == 0) {
                snprintf(meta->cover_path, sizeof(meta->cover_path), "%s", val);
            } else if (strcmp(key, "css") == 0 || strcmp(key, "stylesheet") == 0) {
                snprintf(meta->css_path, sizeof(meta->css_path), "%s", val);
            }
        }
    }

    if (found_closing) {
        if (out_body_start_line) *out_body_start_line = end_line + 1;
        return true;
    }
    return false;
}

/** @brief Generate a valid random/pseudo UUID v4 string. */
static void generate_uuid(char *out_uuid, size_t cap) {
    if (!out_uuid || cap < 46) return;
    unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)getpid();
    unsigned char bytes[16];
    for (int i = 0; i < 16; ++i) {
        seed = seed * 1103515245 + 12345;
        bytes[i] = (unsigned char)((seed >> 16) & 0xFF);
    }
    bytes[6] = (bytes[6] & 0x0F) | 0x40; // Version 4
    bytes[8] = (bytes[8] & 0x3F) | 0x80; // Variant 1

    snprintf(out_uuid, cap,
             "urn:uuid:%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             bytes[0], bytes[1], bytes[2], bytes[3],
             bytes[4], bytes[5], bytes[6], bytes[7],
             bytes[8], bytes[9], bytes[10], bytes[11],
             bytes[12], bytes[13], bytes[14], bytes[15]);
}

/** @brief Determine media-type string from image file extension. */
static const char *get_image_media_type(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (strcasecmp(dot, ".png") == 0) return "image/png";
    if (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0) return "image/jpeg";
    if (strcasecmp(dot, ".gif") == 0) return "image/gif";
    if (strcasecmp(dot, ".svg") == 0) return "image/svg+xml";
    if (strcasecmp(dot, ".webp") == 0) return "image/webp";
    return "application/octet-stream";
}

/* =========================================================================
 * XHTML Chapter Serialization & Markdown Parser
 * ========================================================================= */

typedef enum {
    XHTML_BLK_NONE,
    XHTML_BLK_PARA,
    XHTML_BLK_UL,
    XHTML_BLK_OL,
    XHTML_BLK_TASK,
    XHTML_BLK_QUOTE,
    XHTML_BLK_TABLE
} XhtmlBlockState;

static void close_xhtml_block(EpubStrBuf *sb, XhtmlBlockState *blk) {
    switch (*blk) {
        case XHTML_BLK_PARA:  strbuf_puts(sb, "</p>\n"); break;
        case XHTML_BLK_UL:
        case XHTML_BLK_TASK:  strbuf_puts(sb, "</ul>\n"); break;
        case XHTML_BLK_OL:    strbuf_puts(sb, "</ol>\n"); break;
        case XHTML_BLK_QUOTE: strbuf_puts(sb, "</blockquote>\n"); break;
        case XHTML_BLK_TABLE: strbuf_puts(sb, "</tbody>\n</table>\n"); break;
        case XHTML_BLK_NONE:
        default: break;
    }
    *blk = XHTML_BLK_NONE;
}

/** @brief Record an image reference, returning the bundled relative filename. */
static const char *bundle_image_file(EpubBookContext *ctx, const char *doc_base_dir,
                                     const char *raw_url, size_t url_len,
                                     bool is_cover) {
    char url_str[PATH_MAX];
    if (url_len >= sizeof(url_str)) url_len = sizeof(url_str) - 1;
    memcpy(url_str, raw_url, url_len);
    url_str[url_len] = '\0';
    trim_whitespace(url_str);

    // If already absolute or relative, resolve against doc_base_dir
    char abs_img[PATH_MAX];
    if (url_str[0] == '/') {
        set_str(abs_img, sizeof(abs_img), url_str);
    } else {
        char combined[PATH_MAX * 2];
        int cn = snprintf(combined, sizeof(combined), "%s/%s", doc_base_dir, url_str);
        if (cn < 0 || (size_t)cn >= sizeof(abs_img)) return NULL;
        set_str(abs_img, sizeof(abs_img), combined);
    }

    struct stat st;
    if (stat(abs_img, &st) != 0 || !S_ISREG(st.st_mode)) {
        return NULL; // Image file does not exist on disk
    }

    // Check if already registered
    for (size_t i = 0; i < ctx->image_count; ++i) {
        if (strcmp(ctx->images[i].disk_path, abs_img) == 0) {
            return ctx->images[i].filename;
        }
    }

    if (ctx->image_count >= EPUB_MAX_IMAGES) return NULL;

    size_t idx = ctx->image_count++;
    EpubBundledImage *img = &ctx->images[idx];
    memset(img, 0, sizeof(*img));

    snprintf(img->id, sizeof(img->id), is_cover ? "cover-img" : "img_%03zu", idx + 1);
    const char *slash = strrchr(abs_img, '/');
    const char *base = slash ? slash + 1 : abs_img;
    const char *ext = strrchr(base, '.');
    snprintf(img->filename, sizeof(img->filename), "images/%s%s", img->id, ext ? ext : ".png");
    snprintf(img->disk_path, sizeof(img->disk_path), "%s", abs_img);
    snprintf(img->media_type, sizeof(img->media_type), "%s", get_image_media_type(abs_img));
    img->is_cover = is_cover;

    return img->filename;
}

static void xhtml_write_spans(EpubBookContext *ctx, EpubInternalChapter *ch,
                              const char *raw, const MdParsedLine *pl,
                              size_t start_idx, size_t end_idx,
                              const ExportDocList *docs, size_t doc_idx) {
    size_t s = start_idx;
    EpubStrBuf *sb = &ch->body;
    const char *doc_base = docs->nodes[doc_idx].base_dir;

    while (s < end_idx) {
        MdSpan sp = pl->spans[s];

        if (is_skipped_export_marker(sp.type)) {
            s++;
            continue;
        }

        if (sp.type == MD_FOOTNOTE_REF) {
            const char *fn = raw + sp.start + 2;
            size_t fn_len = (sp.end > sp.start + 3) ? (sp.end - sp.start - 3) : 0;
            char fn_label[64];
            if (fn_len >= sizeof(fn_label)) fn_len = sizeof(fn_label) - 1;
            memcpy(fn_label, fn, fn_len);
            fn_label[fn_len] = '\0';

            strbuf_puts(sb, "<a href=\"#fn-");
            strbuf_xml_escape(sb, fn_label, strlen(fn_label));
            strbuf_puts(sb, "\" id=\"fnref-");
            strbuf_xml_escape(sb, fn_label, strlen(fn_label));
            strbuf_puts(sb, "\" class=\"noteref\" epub:type=\"noteref\"><sup>[");
            strbuf_xml_escape(sb, fn_label, strlen(fn_label));
            strbuf_puts(sb, "]</sup></a>");
            s++;
            continue;
        }

        if (sp.type == MD_AUTO_LINK) {
            const char *url = raw + sp.start;
            size_t ulen = sp.end - sp.start;
            strbuf_puts(sb, "<a href=\"");
            strbuf_xml_escape(sb, url, ulen);
            strbuf_puts(sb, "\">");
            strbuf_xml_escape(sb, url, ulen);
            strbuf_puts(sb, "</a>");
            s++;
            continue;
        }

        if (sp.type == MD_IMAGE_MARKER) {
            const char *alt, *url;
            size_t alt_len, url_len;
            s = extract_link_or_image_span(raw, pl, s, end_idx, &alt, &alt_len, &url, &url_len);

            const char *bundled_name = bundle_image_file(ctx, doc_base, url, url_len, false);
            strbuf_puts(sb, "<img src=\"");
            if (bundled_name) {
                strbuf_puts(sb, bundled_name);
            } else {
                strbuf_xml_escape(sb, url, url_len);
            }
            strbuf_puts(sb, "\" alt=\"");
            strbuf_xml_escape(sb, alt, alt_len);
            strbuf_puts(sb, "\" />");
            continue;
        }

        if (sp.type == MD_LINK_BRACKET && raw[sp.start] == '[') {
            const char *ltext, *url;
            size_t ltext_len, url_len;
            s = extract_link_or_image_span(raw, pl, s, end_idx, &ltext, &ltext_len, &url, &url_len);

            char frag[128] = {0};
            ssize_t target_idx = find_linked_doc_index(docs, doc_idx, url, url_len, frag, sizeof(frag));

            strbuf_puts(sb, "<a href=\"");
            if (target_idx >= 0) {
                // Find chapter filename matching target_idx
                const char *target_anchor = docs->nodes[target_idx].anchor_id;
                const char *target_ch_file = "ch001.xhtml";
                for (size_t c = 0; c < ctx->chapter_count; ++c) {
                    if (strcmp(ctx->chapters[c].doc_anchor_id, target_anchor) == 0) {
                        target_ch_file = ctx->chapters[c].filename;
                        break;
                    }
                }
                if (frag[0]) {
                    char slug[128];
                    slugify_heading(frag, strlen(frag), slug, sizeof(slug));
                    strbuf_printf(sb, "%s#%s", target_ch_file, slug);
                } else {
                    strbuf_puts(sb, target_ch_file);
                }
            } else if (url_len > 0 && url[0] == '#') {
                // Same-document anchor
                char slug[128];
                slugify_heading(url + 1, url_len - 1, slug, sizeof(slug));
                strbuf_printf(sb, "#%s", slug);
            } else {
                strbuf_xml_escape(sb, url, url_len);
            }
            strbuf_puts(sb, "\">");
            strbuf_xml_escape(sb, ltext, ltext_len);
            strbuf_puts(sb, "</a>");
            continue;
        }

        const char *txt = raw + sp.start;
        size_t tlen = sp.end - sp.start;

        switch (sp.type) {
            case MD_BOLD_TEXT:
                strbuf_puts(sb, "<strong>"); strbuf_xml_escape(sb, txt, tlen); strbuf_puts(sb, "</strong>");
                break;
            case MD_ITALIC_TEXT:
                strbuf_puts(sb, "<em>"); strbuf_xml_escape(sb, txt, tlen); strbuf_puts(sb, "</em>");
                break;
            case MD_BOLD_ITALIC_TEXT:
                strbuf_puts(sb, "<strong><em>"); strbuf_xml_escape(sb, txt, tlen); strbuf_puts(sb, "</em></strong>");
                break;
            case MD_STRIKE_TEXT:
                strbuf_puts(sb, "<del>"); strbuf_xml_escape(sb, txt, tlen); strbuf_puts(sb, "</del>");
                break;
            case MD_INLINE_CODE_TEXT:
                strbuf_puts(sb, "<code>"); strbuf_xml_escape(sb, txt, tlen); strbuf_puts(sb, "</code>");
                break;
            default:
                strbuf_xml_escape(sb, txt, tlen);
                break;
        }
        s++;
    }
}

static void start_new_chapter(EpubBookContext *ctx, const char *title, const char *doc_anchor) {
    if (ctx->chapter_count >= EPUB_MAX_CHAPTERS) return;
    size_t idx = ctx->chapter_count++;
    EpubInternalChapter *ch = &ctx->chapters[idx];
    memset(ch, 0, sizeof(*ch));

    snprintf(ch->id, sizeof(ch->id), "ch%03zu", idx + 1);
    snprintf(ch->filename, sizeof(ch->filename), "ch%03zu.xhtml", idx + 1);
    snprintf(ch->title, sizeof(ch->title), "%s", title ? title : "Chapter");
    snprintf(ch->doc_anchor_id, sizeof(ch->doc_anchor_id), "%s", doc_anchor ? doc_anchor : "mde-doc-0");
    strbuf_init(&ch->body);
}

/** @brief Process body lines of a document node into the book chapters. */
static void process_doc_lines(EpubBookContext *ctx, const ExportDocList *docs, size_t doc_idx,
                              size_t start_line, bool split_h1) {
    const Buffer *buf = docs->nodes[doc_idx].buf;
    size_t nlines = buffer_line_count(buf);

    XhtmlBlockState blk = XHTML_BLK_NONE;
    bool in_code = false;
    bool code_tag_open = false;
    char lang[32] = {0};

    // Ensure at least one chapter exists
    if (ctx->chapter_count == 0) {
        const char *t = docs->nodes[doc_idx].title[0] ? docs->nodes[doc_idx].title : "Introduction";
        start_new_chapter(ctx, t, docs->nodes[doc_idx].anchor_id);
    }

    EpubInternalChapter *cur_ch = &ctx->chapters[ctx->chapter_count - 1];

    for (size_t r = start_line; r < nlines; ++r) {
        const char *raw = buffer_line(buf, r);
        size_t len = buffer_line_len(buf, r);
        bool was_in_code = in_code;

        MdParsedLine pl;
        md_parse_line(raw, len, &in_code, lang, sizeof(lang), &pl);

        // Fenced code block transitions
        if (pl.is_code_block && pl.span_count > 0 && pl.spans[0].type == MD_CODE_FENCE) {
            close_xhtml_block(&cur_ch->body, &blk);
            if (!was_in_code && in_code) {
                if (lang[0]) {
                    strbuf_printf(&cur_ch->body, "<pre><code class=\"language-");
                    strbuf_xml_escape(&cur_ch->body, lang, strlen(lang));
                    strbuf_puts(&cur_ch->body, "\">");
                } else {
                    strbuf_puts(&cur_ch->body, "<pre><code>");
                }
                code_tag_open = true;
            } else if (was_in_code && !in_code) {
                strbuf_puts(&cur_ch->body, "</code></pre>\n");
                code_tag_open = false;
            }
            md_parsed_line_free(&pl);
            continue;
        }

        if (code_tag_open) {
            strbuf_xml_escape(&cur_ch->body, raw, len);
            strbuf_putc(&cur_ch->body, '\n');
            md_parsed_line_free(&pl);
            continue;
        }

        if (len == 0) {
            close_xhtml_block(&cur_ch->body, &blk);
            md_parsed_line_free(&pl);
            continue;
        }

        // Heading handling
        if (pl.header_level > 0) {
            close_xhtml_block(&cur_ch->body, &blk);

            char htxt[256];
            size_t hpos = 0;
            for (size_t s = 0; s < pl.span_count; ++s) {
                if (md_is_header_marker(pl.spans[s].type)) continue;
                size_t sl = pl.spans[s].end - pl.spans[s].start;
                if (hpos + sl >= sizeof(htxt) - 1) sl = sizeof(htxt) - 1 - hpos;
                if (sl > 0) { memcpy(htxt + hpos, raw + pl.spans[s].start, sl); hpos += sl; }
            }
            htxt[hpos] = '\0';
            trim_whitespace(htxt);

            char slug[128];
            slugify_heading(htxt, strlen(htxt), slug, sizeof(slug));

            if (pl.header_level == 1 && split_h1) {
                // If current chapter already has text, split into a new chapter!
                if (cur_ch->body.len > 0) {
                    start_new_chapter(ctx, htxt, docs->nodes[doc_idx].anchor_id);
                    cur_ch = &ctx->chapters[ctx->chapter_count - 1];
                } else {
                    snprintf(cur_ch->title, sizeof(cur_ch->title), "%s", htxt);
                }
            } else if (pl.header_level >= 2 && pl.header_level <= 3) {
                if (cur_ch->subhead_count < EPUB_MAX_SUBHEADS) {
                    EpubSubhead *sh = &cur_ch->subheads[cur_ch->subhead_count++];
                    snprintf(sh->title, sizeof(sh->title), "%s", htxt);
                    snprintf(sh->slug, sizeof(sh->slug), "%s", slug);
                    sh->level = pl.header_level;
                }
            }

            if (slug[0]) {
                strbuf_printf(&cur_ch->body, "<h%d id=\"%s\">", pl.header_level, slug);
            } else {
                strbuf_printf(&cur_ch->body, "<h%d>", pl.header_level);
            }
            xhtml_write_spans(ctx, cur_ch, raw, &pl, 0, pl.span_count, docs, doc_idx);
            strbuf_printf(&cur_ch->body, "</h%d>\n", pl.header_level);

        } else if (pl.is_hrule) {
            close_xhtml_block(&cur_ch->body, &blk);
            strbuf_puts(&cur_ch->body, "<hr />\n");

        } else if (pl.is_footnote_def) {
            close_xhtml_block(&cur_ch->body, &blk);
            const char *raw_fn = "";
            size_t id_len = 0;
            if (pl.span_count > 0) {
                extract_footnote_def_id(raw, &pl.spans[0], &raw_fn, &id_len);
            }

            if (cur_ch->footnote_count < EPUB_MAX_FOOTNOTES) {
                EpubFootnote *fn = &cur_ch->footnotes[cur_ch->footnote_count++];
                if (id_len >= sizeof(fn->label)) id_len = sizeof(fn->label) - 1;
                memcpy(fn->label, raw_fn, id_len);
                fn->label[id_len] = '\0';
                snprintf(fn->id, sizeof(fn->id), "%s", fn->label);

                EpubStrBuf tmp;
                strbuf_init(&tmp);
                xhtml_write_spans(ctx, cur_ch, raw, &pl, 1, pl.span_count, docs, doc_idx);
                fn->text_xhtml = strdup(tmp.data ? tmp.data : "");
                strbuf_free(&tmp);
            }

        } else if (pl.is_blockquote) {
            if (blk != XHTML_BLK_QUOTE) {
                close_xhtml_block(&cur_ch->body, &blk);
                strbuf_puts(&cur_ch->body, "<blockquote>\n");
                blk = XHTML_BLK_QUOTE;
            }
            strbuf_puts(&cur_ch->body, "  <p>");
            xhtml_write_spans(ctx, cur_ch, raw, &pl, 0, pl.span_count, docs, doc_idx);
            strbuf_puts(&cur_ch->body, "</p>\n");

        } else if (pl.is_list_item) {
            XhtmlBlockState want = pl.is_task ? XHTML_BLK_TASK
                                              : (is_ordered_bullet(raw, len) ? XHTML_BLK_OL : XHTML_BLK_UL);
            if (blk != want) {
                close_xhtml_block(&cur_ch->body, &blk);
                if (want == XHTML_BLK_TASK) strbuf_puts(&cur_ch->body, "<ul class=\"task-list\">\n");
                else if (want == XHTML_BLK_OL) strbuf_puts(&cur_ch->body, "<ol>\n");
                else strbuf_puts(&cur_ch->body, "<ul>\n");
                blk = want;
            }
            if (pl.is_task) {
                strbuf_printf(&cur_ch->body, "  <li><input type=\"checkbox\" disabled=\"disabled\"%s /> ",
                              pl.task_checked ? " checked=\"checked\"" : "");
            } else {
                strbuf_puts(&cur_ch->body, "  <li>");
            }
            xhtml_write_spans(ctx, cur_ch, raw, &pl, 0, pl.span_count, docs, doc_idx);
            strbuf_puts(&cur_ch->body, "</li>\n");

        } else if (pl.is_table_row) {
            if (is_table_sep_row(raw, len)) {
                md_parsed_line_free(&pl);
                continue;
            }
            bool is_hdr = false;
            if (blk != XHTML_BLK_TABLE) {
                close_xhtml_block(&cur_ch->body, &blk);
                strbuf_puts(&cur_ch->body, "<table>\n");
                blk = XHTML_BLK_TABLE;
                if (r + 1 < nlines &&
                    is_table_sep_row(buffer_line(buf, r + 1), buffer_line_len(buf, r + 1))) {
                    is_hdr = true;
                }
                strbuf_puts(&cur_ch->body, is_hdr ? "<thead>\n" : "<tbody>\n");
            }
            const char *cell_tag = is_hdr ? "th" : "td";
            strbuf_puts(&cur_ch->body, "  <tr>");
            size_t s = 0;
            while (s < pl.span_count) {
                if (pl.spans[s].type == MD_TABLE_BORDER) { s++; continue; }
                size_t cell_end = s;
                while (cell_end < pl.span_count && pl.spans[cell_end].type != MD_TABLE_BORDER) cell_end++;
                strbuf_printf(&cur_ch->body, "<%s>", cell_tag);
                xhtml_write_spans(ctx, cur_ch, raw, &pl, s, cell_end, docs, doc_idx);
                strbuf_printf(&cur_ch->body, "</%s>", cell_tag);
                s = cell_end;
            }
            strbuf_puts(&cur_ch->body, "</tr>\n");
            if (is_hdr) strbuf_puts(&cur_ch->body, "</thead>\n<tbody>\n");

        } else {
            if (blk != XHTML_BLK_PARA) {
                close_xhtml_block(&cur_ch->body, &blk);
                strbuf_puts(&cur_ch->body, "<p>");
                blk = XHTML_BLK_PARA;
            } else {
                strbuf_putc(&cur_ch->body, ' ');
            }
            xhtml_write_spans(ctx, cur_ch, raw, &pl, 0, pl.span_count, docs, doc_idx);
        }

        md_parsed_line_free(&pl);
    }

    if (code_tag_open) strbuf_puts(&cur_ch->body, "</code></pre>\n");
    close_xhtml_block(&cur_ch->body, &blk);
}

/* =========================================================================
 * Full Document XHTML Wrapper Generator
 * ========================================================================= */

static char *build_chapter_complete_xhtml(const EpubBookContext *ctx, size_t ch_idx) {
    const EpubInternalChapter *ch = &ctx->chapters[ch_idx];
    EpubStrBuf sb;
    strbuf_init(&sb);

    const char *lang = ctx->meta.language[0] ? ctx->meta.language : "en";

    if (ctx->version == EPUB_VERSION_3) {
        strbuf_puts(&sb, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        strbuf_puts(&sb, "<!DOCTYPE html>\n");
        strbuf_printf(&sb, "<html xmlns=\"http://www.w3.org/1999/xhtml\" xmlns:epub=\"http://www.idpf.org/2007/ops\" xml:lang=\"%s\" lang=\"%s\">\n", lang, lang);
        strbuf_puts(&sb, "<head>\n");
        strbuf_puts(&sb, "  <meta charset=\"utf-8\" />\n");
        strbuf_puts(&sb, "  <title>");
        strbuf_xml_escape(&sb, ch->title, strlen(ch->title));
        strbuf_puts(&sb, "</title>\n");
        strbuf_puts(&sb, "  <link rel=\"stylesheet\" type=\"text/css\" href=\"stylesheet.css\" />\n");
        strbuf_puts(&sb, "</head>\n");
        strbuf_puts(&sb, "<body>\n");
    } else {
        strbuf_puts(&sb, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        strbuf_puts(&sb, "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.1//EN\" \"http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd\">\n");
        strbuf_printf(&sb, "<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"%s\">\n", lang);
        strbuf_puts(&sb, "<head>\n");
        strbuf_puts(&sb, "  <meta http-equiv=\"Content-Type\" content=\"application/xhtml+xml; charset=utf-8\" />\n");
        strbuf_puts(&sb, "  <title>");
        strbuf_xml_escape(&sb, ch->title, strlen(ch->title));
        strbuf_puts(&sb, "</title>\n");
        strbuf_puts(&sb, "  <link rel=\"stylesheet\" type=\"text/css\" href=\"stylesheet.css\" />\n");
        strbuf_puts(&sb, "</head>\n");
        strbuf_puts(&sb, "<body>\n");
    }

    // Body markup
    if (ch->body.data) {
        strbuf_puts(&sb, ch->body.data);
    }

    // Footnotes block at end of chapter if any
    if (ch->footnote_count > 0) {
        strbuf_puts(&sb, "\n<div class=\"footnotes\" epub:type=\"footnotes\">\n  <hr />\n");
        for (size_t f = 0; f < ch->footnote_count; ++f) {
            const EpubFootnote *fn = &ch->footnotes[f];
            strbuf_printf(&sb, "  <aside id=\"fn-%s\" class=\"footnote\" epub:type=\"footnote\">\n", fn->id);
            strbuf_printf(&sb, "    <p><sup>[%s]</sup> %s <a href=\"#fnref-%s\" class=\"fnback\" role=\"doc-backlink\">↩</a></p>\n",
                          fn->label, fn->text_xhtml ? fn->text_xhtml : "", fn->id);
            strbuf_puts(&sb, "  </aside>\n");
        }
        strbuf_puts(&sb, "</div>\n");
    }

    strbuf_puts(&sb, "</body>\n</html>\n");
    return sb.data;
}

/* =========================================================================
 * Navigation & Metadata Generators (nav.xhtml, toc.ncx, package.opf)
 * ========================================================================= */

static char *build_nav_xhtml(const EpubBookContext *ctx) {
    EpubStrBuf sb;
    strbuf_init(&sb);
    const char *lang = ctx->meta.language[0] ? ctx->meta.language : "en";

    strbuf_puts(&sb, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    strbuf_puts(&sb, "<!DOCTYPE html>\n");
    strbuf_printf(&sb, "<html xmlns=\"http://www.w3.org/1999/xhtml\" xmlns:epub=\"http://www.idpf.org/2007/ops\" xml:lang=\"%s\" lang=\"%s\">\n", lang, lang);
    strbuf_puts(&sb, "<head>\n");
    strbuf_puts(&sb, "  <meta charset=\"utf-8\" />\n");
    strbuf_puts(&sb, "  <title>Table of Contents</title>\n");
    strbuf_puts(&sb, "  <link rel=\"stylesheet\" type=\"text/css\" href=\"stylesheet.css\" />\n");
    strbuf_puts(&sb, "</head>\n");
    strbuf_puts(&sb, "<body>\n");
    strbuf_puts(&sb, "  <nav epub:type=\"toc\" id=\"toc\" role=\"doc-toc\">\n");
    strbuf_puts(&sb, "    <h1>Table of Contents</h1>\n");
    strbuf_puts(&sb, "    <ol>\n");

    for (size_t i = 0; i < ctx->chapter_count; ++i) {
        const EpubInternalChapter *ch = &ctx->chapters[i];
        strbuf_printf(&sb, "      <li><a href=\"%s\">", ch->filename);
        strbuf_xml_escape(&sb, ch->title, strlen(ch->title));
        strbuf_puts(&sb, "</a>");

        if (ch->subhead_count > 0) {
            strbuf_puts(&sb, "\n        <ol>\n");
            for (size_t s = 0; s < ch->subhead_count; ++s) {
                strbuf_printf(&sb, "          <li><a href=\"%s#%s\">", ch->filename, ch->subheads[s].slug);
                strbuf_xml_escape(&sb, ch->subheads[s].title, strlen(ch->subheads[s].title));
                strbuf_puts(&sb, "</a></li>\n");
            }
            strbuf_puts(&sb, "        </ol>\n      ");
        }
        strbuf_puts(&sb, "</li>\n");
    }

    strbuf_puts(&sb, "    </ol>\n");
    strbuf_puts(&sb, "  </nav>\n");
    strbuf_puts(&sb, "</body>\n</html>\n");
    return sb.data;
}

static char *build_toc_ncx(const EpubBookContext *ctx) {
    EpubStrBuf sb;
    strbuf_init(&sb);
    const char *lang = ctx->meta.language[0] ? ctx->meta.language : "en";

    strbuf_puts(&sb, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    strbuf_printf(&sb, "<ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" version=\"2005-1\" xml:lang=\"%s\">\n", lang);
    strbuf_puts(&sb, "  <head>\n");
    strbuf_printf(&sb, "    <meta name=\"dtb:uid\" content=\"%s\" />\n", ctx->meta.identifier);
    strbuf_puts(&sb, "    <meta name=\"dtb:depth\" content=\"2\" />\n");
    strbuf_puts(&sb, "    <meta name=\"dtb:totalPageCount\" content=\"0\" />\n");
    strbuf_puts(&sb, "    <meta name=\"dtb:maxPageNumber\" content=\"0\" />\n");
    strbuf_puts(&sb, "  </head>\n");

    strbuf_puts(&sb, "  <docTitle><text>");
    strbuf_xml_escape(&sb, ctx->meta.title, strlen(ctx->meta.title));
    strbuf_puts(&sb, "</text></docTitle>\n");

    strbuf_puts(&sb, "  <docAuthor><text>");
    strbuf_xml_escape(&sb, ctx->meta.author, strlen(ctx->meta.author));
    strbuf_puts(&sb, "</text></docAuthor>\n");

    strbuf_puts(&sb, "  <navMap>\n");

    int play_order = 1;
    for (size_t i = 0; i < ctx->chapter_count; ++i) {
        const EpubInternalChapter *ch = &ctx->chapters[i];
        strbuf_printf(&sb, "    <navPoint id=\"navPoint-%d\" playOrder=\"%d\">\n", play_order, play_order);
        play_order++;
        strbuf_puts(&sb, "      <navLabel><text>");
        strbuf_xml_escape(&sb, ch->title, strlen(ch->title));
        strbuf_puts(&sb, "</text></navLabel>\n");
        strbuf_printf(&sb, "      <content src=\"%s\" />\n", ch->filename);

        for (size_t s = 0; s < ch->subhead_count; ++s) {
            strbuf_printf(&sb, "      <navPoint id=\"navPoint-%d\" playOrder=\"%d\">\n", play_order, play_order);
            play_order++;
            strbuf_puts(&sb, "        <navLabel><text>");
            strbuf_xml_escape(&sb, ch->subheads[s].title, strlen(ch->subheads[s].title));
            strbuf_puts(&sb, "</text></navLabel>\n");
            strbuf_printf(&sb, "        <content src=\"%s#%s\" />\n", ch->filename, ch->subheads[s].slug);
            strbuf_puts(&sb, "      </navPoint>\n");
        }
        strbuf_puts(&sb, "    </navPoint>\n");
    }

    strbuf_puts(&sb, "  </navMap>\n");
    strbuf_puts(&sb, "</ncx>\n");
    return sb.data;
}

static char *build_package_opf(const EpubBookContext *ctx) {
    EpubStrBuf sb;
    strbuf_init(&sb);
    const char *lang = ctx->meta.language[0] ? ctx->meta.language : "en";

    strbuf_puts(&sb, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");

    if (ctx->version == EPUB_VERSION_3) {
        strbuf_printf(&sb, "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\" unique-identifier=\"book-id\" xml:lang=\"%s\">\n", lang);
        strbuf_puts(&sb, "  <metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n");
        strbuf_printf(&sb, "    <dc:identifier id=\"book-id\">%s</dc:identifier>\n", ctx->meta.identifier);
        strbuf_puts(&sb, "    <dc:title>");
        strbuf_xml_escape(&sb, ctx->meta.title, strlen(ctx->meta.title));
        strbuf_puts(&sb, "</dc:title>\n");
        strbuf_printf(&sb, "    <dc:language>%s</dc:language>\n", lang);
        strbuf_puts(&sb, "    <dc:creator id=\"creator\">");
        strbuf_xml_escape(&sb, ctx->meta.author, strlen(ctx->meta.author));
        strbuf_puts(&sb, "</dc:creator>\n");
        strbuf_puts(&sb, "    <meta refines=\"#creator\" property=\"role\" scheme=\"marc:relators\">aut</meta>\n");
        if (ctx->meta.date[0]) {
            strbuf_printf(&sb, "    <dc:date>%s</dc:date>\n", ctx->meta.date);
        }

        // ISO 8601 UTC timestamp for dcterms:modified
        time_t now = time(NULL);
        struct tm tm_utc;
        gmtime_r(&now, &tm_utc);
        char iso_mod[64];
        strftime(iso_mod, sizeof(iso_mod), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
        strbuf_printf(&sb, "    <meta property=\"dcterms:modified\">%s</meta>\n", iso_mod);

        if (ctx->meta.publisher[0]) {
            strbuf_puts(&sb, "    <dc:publisher>");
            strbuf_xml_escape(&sb, ctx->meta.publisher, strlen(ctx->meta.publisher));
            strbuf_puts(&sb, "</dc:publisher>\n");
        }
        if (ctx->meta.rights[0]) {
            strbuf_puts(&sb, "    <dc:rights>");
            strbuf_xml_escape(&sb, ctx->meta.rights, strlen(ctx->meta.rights));
            strbuf_puts(&sb, "</dc:rights>\n");
        }
        if (ctx->meta.description[0]) {
            strbuf_puts(&sb, "    <dc:description>");
            strbuf_xml_escape(&sb, ctx->meta.description, strlen(ctx->meta.description));
            strbuf_puts(&sb, "</dc:description>\n");
        }
        strbuf_puts(&sb, "  </metadata>\n");

        strbuf_puts(&sb, "  <manifest>\n");
        strbuf_puts(&sb, "    <item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav\" />\n");
        strbuf_puts(&sb, "    <item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\" />\n");
        strbuf_puts(&sb, "    <item id=\"css\" href=\"stylesheet.css\" media-type=\"text/css\" />\n");

        for (size_t i = 0; i < ctx->chapter_count; ++i) {
            strbuf_printf(&sb, "    <item id=\"%s\" href=\"%s\" media-type=\"application/xhtml+xml\" />\n",
                          ctx->chapters[i].id, ctx->chapters[i].filename);
        }
        for (size_t i = 0; i < ctx->image_count; ++i) {
            strbuf_printf(&sb, "    <item id=\"%s\" href=\"%s\" media-type=\"%s\"%s />\n",
                          ctx->images[i].id, ctx->images[i].filename, ctx->images[i].media_type,
                          ctx->images[i].is_cover ? " properties=\"cover-image\"" : "");
        }
        strbuf_puts(&sb, "  </manifest>\n");

        strbuf_puts(&sb, "  <spine toc=\"ncx\">\n");
        for (size_t i = 0; i < ctx->chapter_count; ++i) {
            strbuf_printf(&sb, "    <itemref idref=\"%s\" />\n", ctx->chapters[i].id);
        }
        strbuf_puts(&sb, "  </spine>\n");
        strbuf_puts(&sb, "</package>\n");

    } else {
        // EPUB 2 OPF
        strbuf_puts(&sb, "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"2.0\" unique-identifier=\"book-id\">\n");
        strbuf_puts(&sb, "  <metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:opf=\"http://www.idpf.org/2007/opf\">\n");
        strbuf_printf(&sb, "    <dc:identifier id=\"book-id\" opf:scheme=\"UUID\">%s</dc:identifier>\n", ctx->meta.identifier);
        strbuf_puts(&sb, "    <dc:title>");
        strbuf_xml_escape(&sb, ctx->meta.title, strlen(ctx->meta.title));
        strbuf_puts(&sb, "</dc:title>\n");
        strbuf_printf(&sb, "    <dc:language>%s</dc:language>\n", lang);
        strbuf_puts(&sb, "    <dc:creator opf:role=\"aut\">");
        strbuf_xml_escape(&sb, ctx->meta.author, strlen(ctx->meta.author));
        strbuf_puts(&sb, "</dc:creator>\n");
        if (ctx->meta.date[0]) {
            strbuf_printf(&sb, "    <dc:date>%s</dc:date>\n", ctx->meta.date);
        }
        if (ctx->meta.publisher[0]) {
            strbuf_puts(&sb, "    <dc:publisher>");
            strbuf_xml_escape(&sb, ctx->meta.publisher, strlen(ctx->meta.publisher));
            strbuf_puts(&sb, "</dc:publisher>\n");
        }
        if (ctx->meta.rights[0]) {
            strbuf_puts(&sb, "    <dc:rights>");
            strbuf_xml_escape(&sb, ctx->meta.rights, strlen(ctx->meta.rights));
            strbuf_puts(&sb, "</dc:rights>\n");
        }
        if (ctx->meta.description[0]) {
            strbuf_puts(&sb, "    <dc:description>");
            strbuf_xml_escape(&sb, ctx->meta.description, strlen(ctx->meta.description));
            strbuf_puts(&sb, "</dc:description>\n");
        }
        strbuf_puts(&sb, "  </metadata>\n");

        strbuf_puts(&sb, "  <manifest>\n");
        strbuf_puts(&sb, "    <item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\" />\n");
        strbuf_puts(&sb, "    <item id=\"css\" href=\"stylesheet.css\" media-type=\"text/css\" />\n");
        for (size_t i = 0; i < ctx->chapter_count; ++i) {
            strbuf_printf(&sb, "    <item id=\"%s\" href=\"%s\" media-type=\"application/xhtml+xml\" />\n",
                          ctx->chapters[i].id, ctx->chapters[i].filename);
        }
        for (size_t i = 0; i < ctx->image_count; ++i) {
            strbuf_printf(&sb, "    <item id=\"%s\" href=\"%s\" media-type=\"%s\" />\n",
                          ctx->images[i].id, ctx->images[i].filename, ctx->images[i].media_type);
        }
        strbuf_puts(&sb, "  </manifest>\n");

        strbuf_puts(&sb, "  <spine toc=\"ncx\">\n");
        for (size_t i = 0; i < ctx->chapter_count; ++i) {
            strbuf_printf(&sb, "    <itemref idref=\"%s\" />\n", ctx->chapters[i].id);
        }
        strbuf_puts(&sb, "  </spine>\n");

        strbuf_puts(&sb, "  <guide>\n");
        if (ctx->chapter_count > 0) {
            strbuf_printf(&sb, "    <reference type=\"text\" title=\"Start\" href=\"%s\" />\n", ctx->chapters[0].filename);
        }
        strbuf_puts(&sb, "  </guide>\n");
        strbuf_puts(&sb, "</package>\n");
    }

    return sb.data;
}

static const char *get_default_epub_css(void) {
    return "@charset \"utf-8\";\n"
           "body {\n"
           "  margin: 5% 5%;\n"
           "  font-family: Georgia, serif;\n"
           "  line-height: 1.6;\n"
           "  text-align: justify;\n"
           "  color: #111;\n"
           "}\n"
           "h1, h2, h3, h4, h5, h6 {\n"
           "  font-family: system-ui, -apple-system, sans-serif;\n"
           "  font-weight: bold;\n"
           "  line-height: 1.25;\n"
           "  break-after: avoid;\n"
           "  page-break-after: avoid;\n"
           "  margin-top: 1.5em;\n"
           "  margin-bottom: 0.5em;\n"
           "}\n"
           "h1 { font-size: 1.85em; margin-top: 0.5em; }\n"
           "h2 { font-size: 1.45em; }\n"
           "h3 { font-size: 1.2em; }\n"
           "p { margin: 0 0 0.85em 0; }\n"
           "blockquote {\n"
           "  margin: 1.2em 1.5em;\n"
           "  padding-left: 1em;\n"
           "  border-left: 3px solid #0366d6;\n"
           "  color: #444;\n"
           "  font-style: italic;\n"
           "}\n"
           "pre {\n"
           "  background-color: #f6f8fa;\n"
           "  padding: 0.85em 1em;\n"
           "  border-radius: 5px;\n"
           "  font-family: monospace;\n"
           "  font-size: 0.9em;\n"
           "  white-space: pre-wrap;\n"
           "  word-break: break-all;\n"
           "}\n"
           "code {\n"
           "  font-family: monospace;\n"
           "  font-size: 0.9em;\n"
           "  background-color: #f6f8fa;\n"
           "  padding: 0.15em 0.35em;\n"
           "  border-radius: 3px;\n"
           "}\n"
           "pre code { background-color: transparent; padding: 0; }\n"
           "table { border-collapse: collapse; width: 100%; margin: 1.2em 0; }\n"
           "th, td { border: 1px solid #ccc; padding: 0.4em 0.6em; text-align: left; }\n"
           "th { background-color: #f6f8fa; font-weight: bold; }\n"
           "ul, ol { margin: 0.85em 0; padding-left: 2em; }\n"
           "li { margin-bottom: 0.25em; }\n"
           "ul.task-list { list-style: none; padding-left: 1.2em; }\n"
           "hr { border: none; border-top: 1px solid #ddd; margin: 2em 0; }\n"
           "a { color: #0366d6; text-decoration: underline; }\n"
           ".noteref { text-decoration: none; font-size: 0.8em; vertical-align: super; }\n"
           ".footnote { font-size: 0.85em; border-top: 1px solid #eee; margin-top: 2em; padding-top: 0.5em; }\n"
           ".fnback { text-decoration: none; margin-left: 0.4em; }\n"
           "img { max-width: 100%; height: auto; }\n";
}

/* =========================================================================
 * ZIP Packaging Engine using libzip
 * ========================================================================= */

static bool zip_add_memory(zip_t *za, const char *entry_name, const void *data, size_t len, int comp_method) {
    zip_source_t *s = zip_source_buffer(za, data, len, 0);
    if (!s) return false;
    zip_int64_t idx = zip_file_add(za, entry_name, s, ZIP_FL_OVERWRITE);
    if (idx < 0) {
        zip_source_free(s);
        return false;
    }
    if (comp_method == ZIP_CM_STORE) {
        zip_set_file_compression(za, (zip_uint64_t)idx, ZIP_CM_STORE, 0);
    }
    return true;
}

static bool zip_add_disk_file(zip_t *za, const char *entry_name, const char *disk_path) {
    if (!za || !entry_name || !disk_path || !disk_path[0]) return false;
    struct stat st;
    if (stat(disk_path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return false;
    }
    zip_source_t *s = zip_source_file(za, disk_path, 0, 0);
    if (!s) return false;
    zip_int64_t idx = zip_file_add(za, entry_name, s, ZIP_FL_OVERWRITE);
    if (idx < 0) {
        zip_source_free(s);
        return false;
    }
    return true;
}

/* =========================================================================
 * Master EPUB Conversion Implementation
 * ========================================================================= */

static void report_progress(const EpubOptions *opts, int step, int total, const char *msg) {
    if (opts && opts->progress_cb) {
        opts->progress_cb(step, total, msg, opts->user_data);
    }
}

static void free_book_context(EpubBookContext *ctx, char **chapter_htmls,
                              char *nav_html, char *toc_ncx, char *package_opf) {
    if (!ctx) return;
    if (chapter_htmls) {
        for (size_t i = 0; i < ctx->chapter_count; ++i) free(chapter_htmls[i]);
        free(chapter_htmls);
    }
    free(nav_html);
    free(toc_ncx);
    free(package_opf);
    for (size_t i = 0; i < ctx->chapter_count; ++i) {
        strbuf_free(&ctx->chapters[i].body);
        for (size_t f = 0; f < ctx->chapters[i].footnote_count; ++f) {
            free(ctx->chapters[i].footnotes[f].text_xhtml);
        }
    }
    free(ctx);
}

bool export_builtin_epub(const ExportDocList *docs, const char *out_path,
                         const EpubOptions *user_opts, char *err_msg, size_t err_cap) {
    if (!docs || docs->count == 0 || !out_path || !out_path[0]) {
        set_str(err_msg, err_cap, "Invalid document list or empty output path");
        return false;
    }

    EpubOptions opts;
    if (user_opts) {
        opts = *user_opts;
    } else {
        epub_options_init(&opts);
    }

    EpubBookContext *ctx = (EpubBookContext *)calloc(1, sizeof(EpubBookContext));
    if (!ctx) {
        set_str(err_msg, err_cap, "Out of memory allocating EPUB context");
        return false;
    }
    ctx->version = opts.version;

    // Step 1: Parse metadata & front matter
    report_progress(&opts, 1, 6, "Parsing Markdown & extracting metadata");

    size_t body_start = 0;
    EpubMetadata yaml_meta;
    memset(&yaml_meta, 0, sizeof(yaml_meta));
    bool has_yaml = epub_parse_front_matter(docs->nodes[0].buf, &yaml_meta, &body_start);

    // Merge metadata: CLI/user_opts override YAML front matter
    ctx->meta = has_yaml ? yaml_meta : opts.meta;
    if (opts.meta.title[0]) snprintf(ctx->meta.title, sizeof(ctx->meta.title), "%s", opts.meta.title);
    if (opts.meta.author[0]) snprintf(ctx->meta.author, sizeof(ctx->meta.author), "%s", opts.meta.author);
    if (opts.meta.language[0]) snprintf(ctx->meta.language, sizeof(ctx->meta.language), "%s", opts.meta.language);
    if (opts.meta.identifier[0]) snprintf(ctx->meta.identifier, sizeof(ctx->meta.identifier), "%s", opts.meta.identifier);
    if (opts.meta.date[0]) snprintf(ctx->meta.date, sizeof(ctx->meta.date), "%s", opts.meta.date);
    if (opts.meta.publisher[0]) snprintf(ctx->meta.publisher, sizeof(ctx->meta.publisher), "%s", opts.meta.publisher);
    if (opts.meta.rights[0]) snprintf(ctx->meta.rights, sizeof(ctx->meta.rights), "%s", opts.meta.rights);
    if (opts.meta.description[0]) snprintf(ctx->meta.description, sizeof(ctx->meta.description), "%s", opts.meta.description);
    if (opts.meta.cover_path[0]) snprintf(ctx->meta.cover_path, sizeof(ctx->meta.cover_path), "%s", opts.meta.cover_path);
    if (opts.meta.css_path[0]) snprintf(ctx->meta.css_path, sizeof(ctx->meta.css_path), "%s", opts.meta.css_path);

    // Fallbacks
    if (!ctx->meta.title[0]) {
        if (docs->nodes[0].title[0]) {
            snprintf(ctx->meta.title, sizeof(ctx->meta.title), "%s", docs->nodes[0].title);
        } else {
            snprintf(ctx->meta.title, sizeof(ctx->meta.title), "Untitled Document");
        }
    }
    if (!ctx->meta.author[0]) {
        const char *user = getenv("USER");
        snprintf(ctx->meta.author, sizeof(ctx->meta.author), "%s", user ? user : "Anonymous");
    }
    if (!ctx->meta.language[0]) {
        snprintf(ctx->meta.language, sizeof(ctx->meta.language), "en");
    }
    if (!ctx->meta.identifier[0]) {
        generate_uuid(ctx->meta.identifier, sizeof(ctx->meta.identifier));
    }
    if (!ctx->meta.date[0]) {
        time_t t = time(NULL);
        struct tm tm_utc;
        gmtime_r(&t, &tm_utc);
        strftime(ctx->meta.date, sizeof(ctx->meta.date), "%Y-%m-%d", &tm_utc);
    }

    // Step 2: Split chapters & process content
    report_progress(&opts, 2, 6, "Splitting document into chapters");

    for (size_t d = 0; d < docs->count; ++d) {
        size_t s_line = (d == 0) ? body_start : 0;
        process_doc_lines(ctx, docs, d, s_line, opts.split_h1);
    }

    if (ctx->chapter_count == 0) {
        set_str(err_msg, err_cap, "Document contains no content to export");
        free_book_context(ctx, NULL, NULL, NULL, NULL);
        return false;
    }

    // Register cover image if specified and exists
    if (ctx->meta.cover_path[0]) {
        bundle_image_file(ctx, docs->nodes[0].base_dir, ctx->meta.cover_path, strlen(ctx->meta.cover_path), true);
    }

    // Step 3: Generate complete XHTML for each chapter
    report_progress(&opts, 3, 6, "Generating XHTML chapter documents");

    char **chapter_htmls = (char **)calloc(ctx->chapter_count, sizeof(char *));
    if (!chapter_htmls) {
        set_str(err_msg, err_cap, "Out of memory generating chapter XHTML");
        free_book_context(ctx, NULL, NULL, NULL, NULL);
        return false;
    }
    for (size_t i = 0; i < ctx->chapter_count; ++i) {
        chapter_htmls[i] = build_chapter_complete_xhtml(ctx, i);
        if (!chapter_htmls[i]) {
            set_str(err_msg, err_cap, "Out of memory generating chapter XHTML");
            free_book_context(ctx, chapter_htmls, NULL, NULL, NULL);
            return false;
        }
    }

    // Step 4: Generate Navigation & TOC
    report_progress(&opts, 4, 6, "Generating navigation documents (nav.xhtml & toc.ncx)");

    char *nav_html = (ctx->version == EPUB_VERSION_3) ? build_nav_xhtml(ctx) : NULL;
    char *toc_ncx = build_toc_ncx(ctx);

    // Step 5: Build package.opf and container.xml
    report_progress(&opts, 5, 6, "Building package manifest & metadata (package.opf)");

    char *package_opf = build_package_opf(ctx);
    if ((ctx->version == EPUB_VERSION_3 && !nav_html) || !toc_ncx || !package_opf) {
        set_str(err_msg, err_cap, "Out of memory generating EPUB metadata");
        free_book_context(ctx, chapter_htmls, nav_html, toc_ncx, package_opf);
        return false;
    }
    const char *container_xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                "<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">\n"
                                "  <rootfiles>\n"
                                "    <rootfile full-path=\"EPUB/package.opf\" media-type=\"application/oebps-package+xml\"/>\n"
                                "  </rootfiles>\n"
                                "</container>\n";

    // Step 6: Create ZIP archive using libzip
    report_progress(&opts, 6, 6, "Packaging EPUB archive with libzip");

    int zerr = 0;
    zip_t *za = zip_open(out_path, ZIP_CREATE | ZIP_TRUNCATE, &zerr);
    if (!za) {
        zip_error_t ze;
        zip_error_init_with_code(&ze, zerr);
        set_str(err_msg, err_cap, zip_error_strerror(&ze));
        zip_error_fini(&ze);

        free_book_context(ctx, chapter_htmls, nav_html, toc_ncx, package_opf);
        return false;
    }

    // 1. mimetype: MUST be first entry, uncompressed
    const char *mimetype_str = "application/epub+zip";
    if (!zip_add_memory(za, "mimetype", mimetype_str, 20, ZIP_CM_STORE)) {
        set_str(err_msg, err_cap, "Failed to write mimetype entry to zip");
        zip_discard(za);
        goto cleanup_fail;
    }

    // 2. META-INF/container.xml
    if (!zip_add_memory(za, "META-INF/container.xml", container_xml, strlen(container_xml), ZIP_CM_DEFLATE)) {
        set_str(err_msg, err_cap, "Failed to write container.xml entry to zip");
        zip_discard(za);
        goto cleanup_fail;
    }

    // 3. EPUB/package.opf
    if (!zip_add_memory(za, "EPUB/package.opf", package_opf, strlen(package_opf), ZIP_CM_DEFLATE)) {
        set_str(err_msg, err_cap, "Failed to write package.opf entry to zip");
        zip_discard(za);
        goto cleanup_fail;
    }

    // 4. EPUB/nav.xhtml (for EPUB 3)
    if (nav_html) {
        if (!zip_add_memory(za, "EPUB/nav.xhtml", nav_html, strlen(nav_html), ZIP_CM_DEFLATE)) {
            set_str(err_msg, err_cap, "Failed to write nav.xhtml entry to zip");
            zip_discard(za);
            goto cleanup_fail;
        }
    }

    // 5. EPUB/toc.ncx
    if (!zip_add_memory(za, "EPUB/toc.ncx", toc_ncx, strlen(toc_ncx), ZIP_CM_DEFLATE)) {
        set_str(err_msg, err_cap, "Failed to write toc.ncx entry to zip");
        zip_discard(za);
        goto cleanup_fail;
    }

    // 6. EPUB/stylesheet.css
    if (ctx->meta.css_path[0]) {
        char abs_css[PATH_MAX];
        bool css_path_ok = true;
        if (ctx->meta.css_path[0] == '/') {
            set_str(abs_css, sizeof(abs_css), ctx->meta.css_path);
        } else if (docs->nodes[0].base_dir[0] != '\0') {
            char combined[PATH_MAX * 2];
            int cn = snprintf(combined, sizeof(combined), "%s/%s", docs->nodes[0].base_dir, ctx->meta.css_path);
            if (cn < 0 || (size_t)cn >= sizeof(abs_css)) {
                css_path_ok = false;
            } else {
                set_str(abs_css, sizeof(abs_css), combined);
            }
        } else {
            set_str(abs_css, sizeof(abs_css), ctx->meta.css_path);
        }
        if (!css_path_ok || !zip_add_disk_file(za, "EPUB/stylesheet.css", abs_css)) {
            const char *def_css = get_default_epub_css();
            zip_add_memory(za, "EPUB/stylesheet.css", def_css, strlen(def_css), ZIP_CM_DEFLATE);
        }
    } else {
        const char *def_css = get_default_epub_css();
        zip_add_memory(za, "EPUB/stylesheet.css", def_css, strlen(def_css), ZIP_CM_DEFLATE);
    }

    // 7. Chapters
    for (size_t i = 0; i < ctx->chapter_count; ++i) {
        char ch_entry[128];
        snprintf(ch_entry, sizeof(ch_entry), "EPUB/%s", ctx->chapters[i].filename);
        if (!zip_add_memory(za, ch_entry, chapter_htmls[i], strlen(chapter_htmls[i]), ZIP_CM_DEFLATE)) {
            set_str(err_msg, err_cap, "Failed to write chapter entry to zip");
            zip_discard(za);
            goto cleanup_fail;
        }
    }

    // 8. Bundled Images
    for (size_t i = 0; i < ctx->image_count; ++i) {
        char img_entry[160];
        snprintf(img_entry, sizeof(img_entry), "EPUB/%s", ctx->images[i].filename);
        zip_add_disk_file(za, img_entry, ctx->images[i].disk_path);
    }

    // Close and flush archive to disk
    if (zip_close(za) < 0) {
        set_str(err_msg, err_cap, "Failed to finalize and write EPUB zip archive");
        goto cleanup_fail;
    }

    free_book_context(ctx, chapter_htmls, nav_html, toc_ncx, package_opf);
    return true;

cleanup_fail:
    free_book_context(ctx, chapter_htmls, nav_html, toc_ncx, package_opf);
    return false;
}

bool epub_convert_buffer(const Buffer *buf, const char *out_path, const EpubOptions *opts,
                         char *err_msg, size_t err_cap) {
    if (!buf || !out_path || !out_path[0]) {
        set_str(err_msg, err_cap, "Invalid buffer or empty destination path");
        return false;
    }

    EpubOptions real_opts;
    if (opts) real_opts = *opts;
    else epub_options_init(&real_opts);

    ExportDocList docs;
    if (!export_doc_list_collect(&docs, buf, real_opts.include_linked)) {
        set_str(err_msg, err_cap, "Failed to collect document and linked files");
        return false;
    }

    bool ok = export_builtin_epub(&docs, out_path, &real_opts, err_msg, err_cap);
    export_doc_list_free(&docs);
    return ok;
}

bool epub_convert_file(const char *in_md_path, const char *out_epub_path, const EpubOptions *opts,
                       char *err_msg, size_t err_cap) {
    if (!in_md_path || !in_md_path[0]) {
        set_str(err_msg, err_cap, "Missing input Markdown file path");
        return false;
    }

    char default_out[PATH_MAX];
    if (!out_epub_path || !out_epub_path[0]) {
        export_default_path(in_md_path, EXPORT_EPUB, default_out, sizeof(default_out));
        out_epub_path = default_out;
    }

    Buffer buf;
    buffer_init(&buf);
    if (!buffer_load(&buf, in_md_path)) {
        set_str(err_msg, err_cap, "Cannot open or read input Markdown file");
        buffer_free(&buf);
        return false;
    }

    bool ok = epub_convert_buffer(&buf, out_epub_path, opts, err_msg, err_cap);
    buffer_free(&buf);
    return ok;
}
