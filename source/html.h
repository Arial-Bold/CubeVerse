/* html.h - just enough HTML reading to pick posts, communities and
 * notifications out of Juxt's pages: finding elements by class or id,
 * reading attributes, and getting an element's text (tags removed,
 * entities decoded, folded to the characters CubeVerse's fonts have). */
#pragma once
#include <stdbool.h>
#include <stddef.h>

/* The next start tag at or after p (before end) whose class list contains
 * cls (tag may be NULL for any). Returns a pointer to its '<', or NULL. */
const char *html_find_class(const char *p, const char *end, const char *tag, const char *cls);
/* The same for id="...". */
const char *html_find_id(const char *p, const char *end, const char *id);
/* The next start tag <tag ...> (any attributes). */
const char *html_find_tag(const char *p, const char *end, const char *tag);
/* An attribute of the start tag at t (entities decoded). False if missing. */
bool html_attr(const char *t, const char *name, char *out, size_t cap);
/* True if the start tag at t has class cls. */
bool html_has_class(const char *t, const char *cls);
/* Where the start tag at t ends (just after its '>'). */
const char *html_tag_end(const char *t);
/* Just after the element that starts at t (its matching close tag, or the
 * start tag itself for void elements). */
const char *html_elem_end(const char *t, const char *end);
/* The text inside the element starting at t (or of the range [a, b) for
 * html_text_range): tags removed, <br> and paragraph ends become new lines,
 * runs of spaces collapsed, entities decoded, trimmed. */
void html_inner_text(const char *t, const char *end, char *out, size_t cap);
void html_text_range(const char *a, const char *b, char *out, size_t cap);
/* Decodes &amp; etc. and folds UTF-8 to the fonts' characters in place. */
void html_decode(char *s);
