# Markdown to EPUB converter

## 0. General specifications

This is a specification for a Markdown to EPUB converter. This should be
written in C11 without using external libraries, except zip, this should be a
suitable standard library and not our own invention. That library should be
statically linked to the application.

It shall be incorporated with the mde editor, but also as a separate command
line tool that can be invoked from the CLI by the user.

## 1. Functions needed

These are not in a particular order.

- To understand the latest specification of Markdown and EPUB3 format with fallback to EPUB2.
- Command line switch to choose output between older EPUB2 and EPUB3 format.
- Progress indicator
- Linked files should be separate chapters in the EPUB
- External links should be preserved for the EPUB reader
- A way of specifying metadata to be used in the ebook needs to be implemented and documented
- Should create the needed TOC file
- Must follow proper structures for the EPUB format
- H1 ("#") in markdown starts new chapter and new file in EPUB
- A linked file starts new chapter and new file in EPUB
- output filename should default be input filename, without extention, replaced with ".epub"
- Incorporate this in the mde application to replace reliance of 3rd party 

## 2. Suggested pipeline order

These are in a particular order of suggested way of working. If you have a
better way it's fine to deviate from this.

1. Parse arguments
2. Load configuration
3. Split chapters 
4. Transform Markdown to EPUB XHTML
5. Build the metadata from specification in the MD file. The metadata should
not be visible in the book.
6. Build the manifest/spine
7. Generate the OPF/Nav/ncx
8. Create the EPUB structure
9. Zip it with a standard zip library and create the actual epub file

## 3. When done

When you are done do this:

* Updated this file with the results
* Write a summary of the project in this file
* Include information on the ZIP library used








