/**
 * @file md2epub_main.c
 * @brief Standalone CLI binary for native Markdown to EPUB 2/3 e-book conversion.
 */

#define _POSIX_C_SOURCE 200809L
#include "epub.h"
#include "export.h"
#include "version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <getopt.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>

static void print_usage(const char *prog_name) {
    fprintf(stdout,
        "Usage: %s [OPTIONS] <input.md> [output.epub]\n\n"
        "Convert Markdown files and hypertext document graphs into compliant EPUB 2 or EPUB 3 e-books.\n\n"
        "Options:\n"
        "  -o, --output <file>      Specify output .epub path (default: <input_stem>.epub)\n"
        "  -2, --epub2              Generate legacy EPUB 2.0.1 (default is modern EPUB 3)\n"
        "  -3, --epub3              Generate modern EPUB 3.2 (default)\n"
        "  -t, --title <str>        Override book title\n"
        "  -a, --author <str>       Override primary author / creator\n"
        "  -l, --lang <code>        Override language code (e.g., 'en', 'sv', 'de')\n"
        "      --date <str>         Override publication date (YYYY-MM-DD)\n"
        "      --publisher <str>    Override publisher name\n"
        "      --identifier <str>   Override unique book ID (ISBN, UUID, etc.)\n"
        "      --rights <str>       Override copyright or licensing statement\n"
        "      --description <str>  Override book summary / abstract\n"
        "      --cover <image>      Path to cover image (JPG, PNG, WebP, GIF, SVG)\n"
        "  -c, --css <file>         Path to custom CSS stylesheet (overrides default CSS)\n"
        "      --no-split           Do not split chapters on H1 (keep in single document)\n"
        "      --no-linked          Do not follow or bundle locally linked Markdown files\n"
        "  -q, --quiet              Suppress progress and informational messages\n"
        "  -v, --version            Display version information and exit\n"
        "  -h, --help               Display this help text and exit\n\n"
        "Front Matter:\n"
        "  YAML front matter enclosed in '---' at the top of the input Markdown\n"
        "  file is automatically parsed for metadata and stripped from the book body.\n"
        "  CLI metadata flags override YAML front matter values.\n\n"
        "Examples:\n"
        "  %s my_novel.md\n"
        "  %s book.md -o dist/book.epub --title \"My Book\" --author \"Jane Doe\"\n"
        "  %s -2 --cover cover.jpg -c style.css manual.md\n",
        prog_name, prog_name, prog_name, prog_name);
}

static void print_version(void) {
    fprintf(stdout,
        "md2epub (%s) version %s\n"
        "Built: %s %s\n"
        "Native Markdown to EPUB2/EPUB3 converter using libzip\n",
        MDE_APP_NAME, MDE_VERSION_STRING, MDE_BUILD_DATE, MDE_BUILD_TIME);
}

static void progress_reporter(int current_step, int total_steps, const char *step_name, void *user_data) {
    (void)user_data;
    fprintf(stderr, "[%d/%d] %s...\n", current_step, total_steps, step_name);
    fflush(stderr);
}

int main(int argc, char **argv) {
    EpubOptions opts;
    epub_options_init(&opts);

    char out_path_arg[PATH_MAX] = {0};
    bool quiet = false;

    enum {
        OPT_DATE = 1000,
        OPT_PUBLISHER,
        OPT_IDENTIFIER,
        OPT_RIGHTS,
        OPT_DESCRIPTION,
        OPT_COVER,
        OPT_NO_SPLIT,
        OPT_NO_LINKED
    };

    static struct option long_options[] = {
        {"output",      required_argument, 0, 'o'},
        {"epub2",       no_argument,       0, '2'},
        {"epub3",       no_argument,       0, '3'},
        {"title",       required_argument, 0, 't'},
        {"author",      required_argument, 0, 'a'},
        {"lang",        required_argument, 0, 'l'},
        {"date",        required_argument, 0, OPT_DATE},
        {"publisher",   required_argument, 0, OPT_PUBLISHER},
        {"identifier",  required_argument, 0, OPT_IDENTIFIER},
        {"rights",      required_argument, 0, OPT_RIGHTS},
        {"description", required_argument, 0, OPT_DESCRIPTION},
        {"cover",       required_argument, 0, OPT_COVER},
        {"css",         required_argument, 0, 'c'},
        {"no-split",    no_argument,       0, OPT_NO_SPLIT},
        {"no-linked",   no_argument,       0, OPT_NO_LINKED},
        {"quiet",       no_argument,       0, 'q'},
        {"version",     no_argument,       0, 'v'},
        {"help",        no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    int opt_idx = 0;
    while ((opt = getopt_long(argc, argv, "o:23t:a:l:c:qvh", long_options, &opt_idx)) != -1) {
        switch (opt) {
            case 'o':
                snprintf(out_path_arg, sizeof(out_path_arg), "%s", optarg);
                break;
            case '2':
                opts.version = EPUB_VERSION_2;
                break;
            case '3':
                opts.version = EPUB_VERSION_3;
                break;
            case 't':
                snprintf(opts.meta.title, sizeof(opts.meta.title), "%s", optarg);
                break;
            case 'a':
                snprintf(opts.meta.author, sizeof(opts.meta.author), "%s", optarg);
                break;
            case 'l':
                snprintf(opts.meta.language, sizeof(opts.meta.language), "%s", optarg);
                break;
            case OPT_DATE:
                snprintf(opts.meta.date, sizeof(opts.meta.date), "%s", optarg);
                break;
            case OPT_PUBLISHER:
                snprintf(opts.meta.publisher, sizeof(opts.meta.publisher), "%s", optarg);
                break;
            case OPT_IDENTIFIER:
                snprintf(opts.meta.identifier, sizeof(opts.meta.identifier), "%s", optarg);
                break;
            case OPT_RIGHTS:
                snprintf(opts.meta.rights, sizeof(opts.meta.rights), "%s", optarg);
                break;
            case OPT_DESCRIPTION:
                snprintf(opts.meta.description, sizeof(opts.meta.description), "%s", optarg);
                break;
            case OPT_COVER:
                snprintf(opts.meta.cover_path, sizeof(opts.meta.cover_path), "%s", optarg);
                break;
            case 'c':
                snprintf(opts.meta.css_path, sizeof(opts.meta.css_path), "%s", optarg);
                break;
            case OPT_NO_SPLIT:
                opts.split_h1 = false;
                break;
            case OPT_NO_LINKED:
                opts.include_linked = false;
                break;
            case 'q':
                quiet = true;
                break;
            case 'v':
                print_version();
                return 0;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "Error: Missing input Markdown file.\n\n");
        print_usage(argv[0]);
        return 1;
    }

    const char *in_path = argv[optind++];
    char out_path[PATH_MAX] = {0};

    if (out_path_arg[0]) {
        snprintf(out_path, sizeof(out_path), "%s", out_path_arg);
    } else if (optind < argc) {
        snprintf(out_path, sizeof(out_path), "%s", argv[optind++]);
    } else {
        export_default_path(in_path, EXPORT_EPUB, out_path, sizeof(out_path));
    }

    if (!quiet) {
        opts.progress_cb = progress_reporter;
    }

    char err_msg[512] = {0};
    if (!epub_convert_file(in_path, out_path, &opts, err_msg, sizeof(err_msg))) {
        fprintf(stderr, "md2epub error: %s\n", err_msg[0] ? err_msg : "Unknown error during conversion");
        return 1;
    }

    if (!quiet) {
        struct stat st;
        long long fsize = 0;
        if (stat(out_path, &st) == 0) {
            fsize = (long long)st.st_size;
        }
        fprintf(stdout, "Successfully generated EPUB %d e-book: %s (%lld bytes)\n",
                opts.version == EPUB_VERSION_2 ? 2 : 3, out_path, fsize);
    }

    return 0;
}
