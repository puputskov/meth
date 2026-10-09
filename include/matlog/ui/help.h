#ifndef MATLOG_UI_HELP_H
#define MATLOG_UI_HELP_H

#include <matlog/docs/docs.h>
#include <matlog/platform/key.h>

/*
 * The help viewer (plan section 4.14): the state of a full screen page of
 * the documentation, with the keys that drive it. It draws nothing; the
 * screen draws it from this state, and the tests drive it with keys.
 *
 *	Up Down        scroll a line (or move in a list of results)
 *	Page Up/Down   scroll a page;  Home End  the top and the bottom
 *	Tab Shift+Tab  the next and the previous link of the page
 *	Enter          follow the link (or open the page of a result)
 *	Backspace      back to the page before
 *	h              the index page
 *	/              search (the command line with "help " in it)
 *	q Esc          close the viewer
 *
 * ml_help_open takes what was typed after :help: nothing is the index page,
 * the id of a page is that page, anything else is a search; a search that
 * finds one page opens it, one that finds more shows the list.
 */

typedef struct MlHelpMark
{
	uint64_t page;
	uint64_t top;
	uint64_t link;
	int32_t  has_link;
	int32_t  results;
} MlHelpMark;

ML_VEC_DEFINE (MlHelpMarkVec, MlHelpMark)

typedef struct MlHelp
{
	MlDocs        docs;
	int32_t       loaded;
	int32_t       open;      /* the viewer is shown */
	int32_t       results;   /* a list of search results, not a page */
	uint64_t      page;      /* the page shown */
	uint64_t      top;       /* the first line shown (page or list) */
	int32_t       has_link;
	uint64_t      link;      /* the selected link, counted over the page */
	MlDocHitVec   hits;
	uint64_t      hit;       /* the selected result */
	MlStr         query;
	MlHelpMarkVec history;
	uint32_t      cols;      /* the size of the screen, see ml_help_set_view */
	uint32_t      rows;
} MlHelp;

void ml_help_init (MlHelp *h);
void ml_help_free (MlHelp *h);

/* The size of the screen: the page is laid out for it. */
void ml_help_set_view (MlHelp *h, uint32_t cols, uint32_t rows);

/*
 * Opens the viewer. ML_ERR_INVALID when a search finds nothing (the viewer
 * stays closed), ML_ERR_PARSE when a page of the documentation is broken.
 */
MlResult ml_help_open (MlHelp *h, MlSlice topic);

/*
 * A key. The viewer closes itself (open is 0 afterwards); search is 1 when
 * the key asks for a search to be typed.
 */
MlResult ml_help_key (MlHelp *h, MlKey key, int32_t *search);

/* The columns a page is laid out in on this screen. */
uint64_t ml_help_width (const MlHelp *h);

/* The lines of the page shown, laid out for the screen. */
MlResult ml_help_layout (const MlHelp *h, MlDocLineVec *lines);

/* The number of links in the lines, and where the n-th is. */
uint64_t ml_help_link_count (const MlDocLineVec *lines);
void ml_help_link_place (
	const MlDocLineVec *lines,
	uint64_t           n,
	uint64_t           *line,
	uint64_t           *in_line
);

#endif /* MATLOG_UI_HELP_H */
