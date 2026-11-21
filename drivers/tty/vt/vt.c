/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 virtual terminal — single cursor_pos for /dev/console, TTY echo,
 * kernel print() and klog: ANSI/CSI parsing, cursor, alternate screen
 * and scrollback, drawn through the console core. A backend hardware
 * cursor is used when the video ops provide one; otherwise the cell
 * under the cursor is inverted.
 */

#include <ir0/console_backend.h>
#include <ir0/ktm/klog.h>
#include <ir0/printk_console.h>
#include <ir0/vt.h>
#include "video/console.h"
#include <stddef.h>
#include <stdint.h>

#define VT_COLOR_DEFAULT 0x07u /* light grey on black */

#define CSI_NONE    0
#define CSI_ESC     1
#define CSI_BRACKET 2

#define SCROLLBACK_LINES 200

/* Linear cursor: row * cols + col. */
static int cursor_pos;

static uint8_t render_color = VT_COLOR_DEFAULT;
static int render_cursor_visible;
static int render_cursor_row;
static int render_cursor_col;
static char render_cursor_under_ch = ' ';
static uint8_t render_cursor_under_color = VT_COLOR_DEFAULT;
static int csi_state;
static int csi_param;
static int csi_params[4];
static int csi_param_count;
static int csi_priv; /* ESC[? … private mode */
static int render_cursor_enabled = 1; /* DECTCEM: 0 = civis, 1 = cnorm */

static int printk_to_screen = 1;
static int userspace_gui_first_draw_tag;

/*
 * Alternate screen buffer (xterm ctlseqs DECSET 1049, legacy 47; terminfo
 * smcup/rmcup): entering saves the screen and cursor, leaving restores
 * them, so a full-screen program never paints over the scrollback.
 */
static uint16_t alt_saved_screen[CONSOLE_MAX_HEIGHT][CONSOLE_MAX_WIDTH];
static int alt_saved_cursor_pos;
static int alt_saved_rows;
static int alt_saved_cols;
static int alt_screen_active;

static void sgr_reset_attrs(void);
static void sgr_compose(uint8_t *color);
static void csi_clear_screen(int cols, int rows, uint8_t color);
static void render_show_cursor(uint8_t color);

static void alt_screen_save(int cols, int rows)
{
	int r;
	int c;

	if (cols > CONSOLE_MAX_WIDTH)
		cols = CONSOLE_MAX_WIDTH;
	if (rows > CONSOLE_MAX_HEIGHT)
		rows = CONSOLE_MAX_HEIGHT;

	for (r = 0; r < rows; r++)
	{
		for (c = 0; c < cols; c++)
			alt_saved_screen[r][c] = console_get_cell(r, c);
	}

	alt_saved_cursor_pos = cursor_pos;
	alt_saved_rows = rows;
	alt_saved_cols = cols;
}

static void alt_screen_restore(int cols, int rows)
{
	int r;
	int c;
	int sr = alt_saved_rows;
	int sc = alt_saved_cols;

	if (sr <= 0 || sc <= 0)
		return;
	if (sr > rows)
		sr = rows;
	if (sc > cols)
		sc = cols;

	for (r = 0; r < sr; r++)
	{
		for (c = 0; c < sc; c++)
		{
			uint16_t cell = alt_saved_screen[r][c];

			console_put_cell(r, c, (char)(cell & 0xFF),
					 (uint8_t)(cell >> 8));
		}
	}

	cursor_pos = alt_saved_cursor_pos;
	if (cursor_pos < 0 || cursor_pos >= cols * rows)
		cursor_pos = (sr - 1) * cols;
}

static void alt_screen_enter(int cols, int rows, uint8_t color)
{
	if (!alt_screen_active)
	{
		alt_screen_save(cols, rows);
		alt_screen_active = 1;
	}
	csi_clear_screen(cols, rows, color);
	cursor_pos = 0;
	sgr_reset_attrs();
	sgr_compose(&render_color);
}

static void alt_screen_leave(int cols, int rows)
{
	if (!alt_screen_active)
		return;

	alt_screen_restore(cols, rows);
	alt_screen_active = 0;
}

static void csi_reset(void)
{
	csi_state = CSI_NONE;
	csi_param = 0;
	csi_param_count = 0;
	csi_priv = 0;
}

static void csi_clear_from_cursor(int cols, int rows, uint8_t color)
{
	int row = cursor_pos / cols;
	int col = cursor_pos % cols;
	int r;
	int c;

	for (r = row; r < rows; r++)
	{
		int start = (r == row) ? col : 0;

		for (c = start; c < cols; c++)
			console_put_cell(r, c, ' ', color);
	}
}

/* CSI J mode 1 — erase from start of screen through cursor (inclusive). */
static void csi_clear_to_cursor(int cols, int rows, uint8_t color)
{
	int row = cursor_pos / cols;
	int col = cursor_pos % cols;
	int r;
	int c;

	(void)rows;
	for (r = 0; r <= row; r++)
	{
		int end = (r == row) ? col : (cols - 1);

		for (c = 0; c <= end; c++)
			console_put_cell(r, c, ' ', color);
	}
}

static void csi_clear_screen(int cols, int rows, uint8_t color)
{
	int r;
	int c;

	for (r = 0; r < rows; r++)
		for (c = 0; c < cols; c++)
			console_put_cell(r, c, ' ', color);
}

/*
 * SGR (ECMA-48 8.3.117) attributes persist until changed, across CSI
 * sequences. Bold set before the colour (ESC[1;34m) must still brighten
 * that colour, and reverse video (SGR 7) swaps foreground and background.
 */
static uint8_t sgr_fg = VT_COLOR_DEFAULT & 0x0F;
static uint8_t sgr_bg = (VT_COLOR_DEFAULT >> 4) & 0x0F;
static int sgr_bold;
static int sgr_reverse;

static void sgr_compose(uint8_t *color)
{
	uint8_t fg = sgr_fg & 0x0F;
	uint8_t bg = sgr_bg & 0x0F;

	if (sgr_bold && fg < 8)
		fg = (uint8_t)(fg + 8);
	if (sgr_reverse)
	{
		uint8_t tmp = fg;

		fg = bg;
		bg = tmp;
	}
	*color = (uint8_t)((bg << 4) | fg);
}

static void sgr_reset_attrs(void)
{
	sgr_fg = VT_COLOR_DEFAULT & 0x0F;
	sgr_bg = (VT_COLOR_DEFAULT >> 4) & 0x0F;
	sgr_bold = 0;
	sgr_reverse = 0;
}

static void sgr_apply(int *params, int count, uint8_t *color)
{
	int i;

	if (count == 0)
	{
		sgr_reset_attrs();
		sgr_compose(color);
		return;
	}

	for (i = 0; i < count; i++)
	{
		int p = params[i];

		if (p == 0)
			sgr_reset_attrs();
		else if (p == 1)
			sgr_bold = 1;
		else if (p == 22)
			sgr_bold = 0;
		else if (p == 7)
			sgr_reverse = 1;
		else if (p == 27)
			sgr_reverse = 0;
		else if (p >= 30 && p <= 37)
			sgr_fg = (uint8_t)(p - 30);
		else if (p >= 90 && p <= 97)
			sgr_fg = (uint8_t)(p - 90 + 8);
		else if (p >= 40 && p <= 47)
			sgr_bg = (uint8_t)(p - 40);
		else if (p >= 100 && p <= 107)
			sgr_bg = (uint8_t)(p - 100 + 8);
		else if (p == 39)
			sgr_fg = VT_COLOR_DEFAULT & 0x0F;
		else if (p == 49)
			sgr_bg = (VT_COLOR_DEFAULT >> 4) & 0x0F;
	}
	sgr_compose(color);
}

static void csi_apply(char cmd, int cols, int rows, uint8_t *color)
{
	int n = 1;
	int row;
	int col;
	int i;

	if (csi_param_count > 0)
		n = csi_params[0];
	if (n <= 0)
		n = 1;

	switch (cmd)
	{
	case 'm':
		sgr_apply(csi_params, csi_param_count, color);
		break;
	case 'A':
		if (cursor_pos >= n * cols)
			cursor_pos -= n * cols;
		else
			cursor_pos = (cursor_pos / cols) * cols;
		break;
	case 'B':
		cursor_pos += n * cols;
		if (cursor_pos >= cols * rows)
			cursor_pos = (rows - 1) * cols + (cursor_pos % cols);
		break;
	case 'h':
	case 'l':
		/* ESC[?25h / ESC[?25l — show / hide cursor (DECTCEM). */
		if (csi_priv && n == 25)
			render_cursor_enabled = (cmd == 'h') ? 1 : 0;
		/* ESC[?47h/l and ESC[?1049h/l — alternate screen buffer. */
		else if (csi_priv && (n == 47 || n == 1049))
		{
			if (cmd == 'h')
				alt_screen_enter(cols, rows, *color);
			else
				alt_screen_leave(cols, rows);
		}
		break;
	case 'J':
	{
		/* ECMA-48 ED: 0=from cursor, 1=to cursor, 2=entire screen. */
		int mode = csi_param_count > 0 ? csi_params[0] : 0;

		if (mode == 2 || mode == 3)
		{
			csi_clear_screen(cols, rows, *color);
			if (mode == 2)
				cursor_pos = 0;
		}
		else if (mode == 1)
			csi_clear_to_cursor(cols, rows, *color);
		else
			csi_clear_from_cursor(cols, rows, *color);
		break;
	}
	case 'D':
		if (cursor_pos >= n)
			cursor_pos -= n;
		else
			cursor_pos = (cursor_pos / cols) * cols;
		break;
	case 'C':
		cursor_pos += n;
		if (cursor_pos >= cols * rows)
			cursor_pos = cols * rows - 1;
		break;
	case 'K':
	{
		int mode = csi_param_count > 0 ? csi_params[0] : 0;

		row = cursor_pos / cols;
		if (mode == 2)
		{
			for (i = 0; i < cols; i++)
				console_put_cell(row, i, ' ', *color);
		}
		else if (mode == 1)
		{
			col = cursor_pos % cols;
			for (i = 0; i <= col; i++)
				console_put_cell(row, i, ' ', *color);
		}
		else
		{
			col = cursor_pos % cols;
			for (i = col; i < cols; i++)
				console_put_cell(row, i, ' ', *color);
		}
		break;
	}
	case 'G':
		col = (csi_param_count > 0 ? csi_params[0] : 1) - 1;
		if (col < 0)
			col = 0;
		if (col >= cols)
			col = cols - 1;
		cursor_pos = (cursor_pos / cols) * cols + col;
		break;
	case 'H':
	case 'f':
	{
		int crow = 1;
		int ccol = 1;

		if (csi_param_count >= 1)
			crow = csi_params[0];
		if (csi_param_count >= 2)
			ccol = csi_params[1];
		if (crow < 1)
			crow = 1;
		if (ccol < 1)
			ccol = 1;
		crow--;
		ccol--;
		if (crow >= rows)
			crow = rows - 1;
		if (ccol >= cols)
			ccol = cols - 1;
		cursor_pos = crow * cols + ccol;
		break;
	}
	default:
		break;
	}
}

static int csi_feed(char c, int cols, int rows, uint8_t *color)
{
	if (csi_state == CSI_ESC)
	{
		if (c == '[')
		{
			csi_state = CSI_BRACKET;
			csi_param = 0;
			csi_param_count = 0;
			return 1;
		}
		csi_reset();
		return 0;
	}

	if (csi_state == CSI_BRACKET)
	{
		if (c >= '0' && c <= '9')
		{
			csi_param = csi_param * 10 + (c - '0');
			return 1;
		}
		if (c == ';')
		{
			if (csi_param_count < 4)
				csi_params[csi_param_count++] = csi_param;
			csi_param = 0;
			return 1;
		}
		if (c == '?')
		{
			csi_priv = 1;
			return 1;
		}
		if (c >= 0x40 && c <= 0x7e)
		{
			if (csi_param_count < 4)
				csi_params[csi_param_count++] = csi_param;
			csi_apply(c, cols, rows, color);
			csi_reset();
			return 1;
		}
		csi_reset();
		return 0;
	}

	if ((unsigned char)c == 0x1b)
	{
		csi_state = CSI_ESC;
		return 1;
	}

	return 0;
}

static int render_cols(void)
{
	if (console_use_framebuffer())
		return console_get_width();
	return CONSOLE_WIDTH;
}

static int render_rows(void)
{
	if (console_use_framebuffer())
		return console_get_height();
	return CONSOLE_HEIGHT;
}

static void render_erase_cursor(int cols, int rows, uint8_t color)
{
	(void)color;
	if (!render_cursor_visible)
		return;
	if (render_cursor_row < 0 || render_cursor_row >= rows)
		return;
	if (render_cursor_col < 0 || render_cursor_col >= cols)
		return;
	/* Restore glyph wiped by the block cursor (ONLCR: CR paints col 0). */
	console_put_cell(render_cursor_row, render_cursor_col,
			 render_cursor_under_ch, render_cursor_under_color);
	render_cursor_visible = 0;
}

static void render_reset(uint8_t color)
{
	csi_reset();
	sgr_reset_attrs();
	/* Caller may pass an explicit clear color; keep SGR defaults unless
	 * it matches the composed default (typical product path). */
	render_color = color;
	if (color == VT_COLOR_DEFAULT)
		sgr_compose(&render_color);
	render_cursor_visible = 0;
	render_cursor_row = 0;
	render_cursor_col = 0;
	cursor_pos = 0;
	alt_screen_active = 0;
	alt_saved_rows = 0;
	alt_saved_cols = 0;
	alt_saved_cursor_pos = 0;
	console_clear(color);
	(void)console_set_cursor(0, 0, 0);
}

static void render_putchar(char c, uint8_t color)
{
	int cols = render_cols();
	int rows = render_rows();
	int row;
	int col;
	uint8_t draw;

	(void)color;
	draw = render_color;

	/* Guard against soft geometry / early boot (never divide by zero). */
	if (cols <= 0)
		cols = CONSOLE_WIDTH;
	if (rows <= 0)
		rows = CONSOLE_HEIGHT;

	if (csi_feed(c, cols, rows, &render_color))
	{
		draw = render_color;
		/*
		 * Only refresh the hardware cursor when a full CSI sequence
		 * completes (or civis). Intermediate ESC/[ / digits must not
		 * paint: output may wrap every glyph in DECTCEM hide/show.
		 */
		if (csi_state == CSI_NONE)
		{
			if (render_cursor_enabled)
				render_show_cursor(draw);
			else
				render_erase_cursor(cols, rows, draw);
		}
		return;
	}

	render_erase_cursor(cols, rows, draw);

	if (c == '\n')
	{
		csi_reset();
		cursor_pos = (cursor_pos / cols + 1) * cols;
		if (cursor_pos >= cols * rows)
		{
			console_scroll_up(draw);
			cursor_pos = (rows - 1) * cols;
		}
	}
	else if (c == '\r')
	{
		csi_reset();
		cursor_pos = (cursor_pos / cols) * cols;
	}
	else if (c == '\b' || c == 127)
	{
		if (cursor_pos > 0)
		{
			cursor_pos--;
			row = cursor_pos / cols;
			col = cursor_pos % cols;
			console_put_cell(row, col, ' ', draw);
		}
	}
	else if (c == '\t')
	{
		col = cursor_pos % cols;
		{
			int next = (col + 8) & ~7;

			if (next >= cols)
			{
				cursor_pos = (cursor_pos / cols + 1) * cols;
				if (cursor_pos >= cols * rows)
				{
					console_scroll_up(draw);
					cursor_pos = (rows - 1) * cols;
				}
			}
			else
			{
				cursor_pos = (cursor_pos / cols) * cols + next;
			}
		}
	}
	else if ((unsigned char)c >= ' ')
	{
		row = cursor_pos / cols;
		col = cursor_pos % cols;
		console_put_cell(row, col, c, draw);
		cursor_pos++;
		if (cursor_pos >= cols * rows)
		{
			console_scroll_up(draw);
			cursor_pos = (rows - 1) * cols;
		}
	}

	if (render_cursor_enabled)
		render_show_cursor(draw);
	else
		render_erase_cursor(cols, rows, draw);
}

/* Kernel text: draw with @attr; the SGR colour of the output stream is kept. */
static void render_putchar_attr(char c, uint8_t attr)
{
	uint8_t sgr_color = render_color;

	render_color = attr;
	render_putchar(c, attr);
	render_color = sgr_color;
}

static void render_show_cursor(uint8_t color)
{
	int cols = render_cols();
	int rows = render_rows();
	int row = cursor_pos / cols;
	int col = cursor_pos % cols;
	uint8_t cur_color;
	uint16_t cell;

	(void)color;
	if (!render_cursor_enabled)
	{
		render_erase_cursor(cols, rows, color);
		if (row >= 0 && row < rows && col >= 0 && col < cols)
			(void)console_set_cursor(row, col, 0);
		return;
	}
	render_erase_cursor(cols, rows, color);

	if (row < 0 || row >= rows || col < 0 || col >= cols)
		return;

	if (console_set_cursor(row, col, 1))
		return;

	cell = console_get_cell(row, col);
	render_cursor_under_ch = (char)(cell & 0xFF);
	render_cursor_under_color = (uint8_t)(cell >> 8);
	/* Invert fg/bg; draw without clobbering the shadow under-glyph. */
	cur_color = (uint8_t)(((render_cursor_under_color & 0xF0) >> 4) |
			      ((render_cursor_under_color & 0x0F) << 4));
	console_draw_cell(row, col, render_cursor_under_ch, cur_color);
	render_cursor_visible = 1;
	render_cursor_row = row;
	render_cursor_col = col;
}

static int render_cursor_x(void)
{
	int cols = render_cols();

	if (cols <= 0)
		return 0;
	return cursor_pos % cols;
}


/*
 * Scrollback: circular buffer of lines for console scroll (Page Up/Down).
 * Width matches CONSOLE_MAX_WIDTH so FB fitted geometry (e.g. ~87 cols)
 * does not OOB-read past a VGA-only 80-wide line.
 */
static uint16_t scrollback[SCROLLBACK_LINES][CONSOLE_MAX_WIDTH];
static size_t total_lines_written;	/* Total lines ever appended */
static size_t current_col;		/* Column in current line */
static int scroll_offset;		/* 0 = live at bottom; >0 = scrolled up */
static uint8_t scrollback_color = 0x0F;

static int scrollback_cols(void)
{
	int cols = render_cols();

	if (cols > CONSOLE_MAX_WIDTH)
		cols = CONSOLE_MAX_WIDTH;
	if (cols < 1)
		cols = 1;
	return cols;
}

/*
 * Redraw the visible rows from scrollback. first_line is the logical line
 * index of the top row (can be negative; then top rows are blank). The
 * bottom row shows line total_lines_written.
 */
static void redraw_from_scrollback(void)
{
	int cols = render_cols();
	int rows = render_rows();
	int first_line = (int)((long)total_lines_written - (rows - 1) -
			       scroll_offset);
	int row;
	int col;

	for (row = 0; row < rows; row++)
	{
		int line_idx = first_line + row;

		if (line_idx < 0)
		{
			for (col = 0; col < cols; col++)
				console_put_cell(row, col, ' ', scrollback_color);
		}
		else
		{
			size_t buf_idx = (size_t)line_idx % SCROLLBACK_LINES;
			int maxc = cols < CONSOLE_MAX_WIDTH ? cols : CONSOLE_MAX_WIDTH;

			for (col = 0; col < maxc; col++)
			{
				uint16_t cell = scrollback[buf_idx][col];

				console_put_cell(row, col, (char)(cell & 0xFF),
						 (uint8_t)(cell >> 8));
			}
			for (col = maxc; col < cols; col++)
				console_put_cell(row, col, ' ', scrollback_color);
		}
	}
	if (scroll_offset == 0)
		current_col = (size_t)(cursor_pos % cols);
	else
		cursor_pos = (rows - 1) * cols + (int)current_col;
}

void vt_scroll(int delta)
{
	int rows = render_rows();
	int max_scroll = (int)(total_lines_written + 1) - rows;

	if (max_scroll < 0)
		max_scroll = 0;
	scroll_offset += delta;
	if (scroll_offset < 0)
		scroll_offset = 0;
	if (scroll_offset > max_scroll)
		scroll_offset = max_scroll;
	redraw_from_scrollback();
}

void vt_clear(uint8_t color)
{
	uint16_t blank = ((uint16_t)color << 8) | ' ';
	int i;
	int c;

	render_reset(color);
	scrollback_color = color;
	scroll_offset = 0;
	total_lines_written = 0;
	current_col = 0;

	for (i = 0; i < SCROLLBACK_LINES; i++)
	{
		for (c = 0; c < CONSOLE_MAX_WIDTH; c++)
			scrollback[i][c] = blank;
	}
}

void vt_init(void)
{
	int i;
	int c;

	total_lines_written = 0;
	current_col = 0;
	scroll_offset = 0;
	for (i = 0; i < SCROLLBACK_LINES; i++)
		for (c = 0; c < CONSOLE_MAX_WIDTH; c++)
			scrollback[i][c] = (scrollback_color << 8) | ' ';
}

/*
 * One glyph into the renderer and the scrollback. @attr draws with @color
 * (kernel text) instead of the SGR colour of the output stream.
 */
static void vt_put(char c, uint8_t color, int attr)
{
	int cols;
	int pos;
	size_t line_idx;

	scrollback_color = color;

	if (scroll_offset > 0)
	{
		scroll_offset = 0;
		redraw_from_scrollback();
	}

	if (attr)
		render_putchar_attr(c, color);
	else
		render_putchar(c, color);

	line_idx = total_lines_written % SCROLLBACK_LINES;
	cols = scrollback_cols();
	if (c == '\n')
	{
		size_t j;

		for (j = current_col; j < (size_t)cols; j++)
			scrollback[line_idx][j] = (color << 8) | ' ';
		total_lines_written++;
		current_col = 0;
	}
	else if (c == '\b' || c == 127)
	{
		current_col = (size_t)(cursor_pos % cols);
		if (current_col < (size_t)cols)
			scrollback[line_idx][current_col] = (color << 8) | ' ';
	}
	else if ((unsigned char)c >= ' ')
	{
		pos = cursor_pos - 1;
		if (pos >= 0)
		{
			int sc = pos % cols;

			if (sc >= 0 && sc < cols)
				scrollback[line_idx][sc] = (color << 8) | (uint8_t)c;
		}
		current_col = (size_t)(cursor_pos % cols);
	}
	else
	{
		current_col = (size_t)(cursor_pos % cols);
	}
}

void vt_putchar(char c, uint8_t color)
{
	vt_put(c, color, 0);
}

void vt_putchar_attr(char c, uint8_t color)
{
	vt_put(c, color, 1);
}

void vt_print(const char *str, uint8_t color)
{
	if (!str)
		return;
	while (*str)
	{
		vt_putchar(*str, color);
		str++;
	}
}

void vt_show_cursor(uint8_t color)
{
	render_show_cursor(color);
}

int vt_cursor_x(void)
{
	return render_cursor_x();
}

static void vt_console_write(const char *str)
{
	if (printk_to_screen)
		vt_print(str, 0x07);
}

static struct console vt_console = {
	.name = "vt",
	.write = vt_console_write,
	.flags = CON_MIRROR,
};

void vt_register_printk_console(void)
{
	register_console(&vt_console);
}

int vt_printk_to_screen(void)
{
	return printk_to_screen;
}

void vt_printk_screen_off(void)
{
	printk_to_screen = 0;
}

void vt_printk_screen_on(void)
{
	/*
	 * Re-enable kernel text output to the active console. After the userspace
	 * handoff printk_to_screen is 0, so a panic drew nothing on the FB/GTK
	 * screen (print()/clear_screen() were gated). Turn it back on so the panic
	 * banner and the mirrored dump reach the screen, not just serial.
	 */
	printk_to_screen = 1;
}

void vt_note_userspace_draw(size_t len)
{
	if (!printk_to_screen && console_use_framebuffer() &&
	    len > 0 && !userspace_gui_first_draw_tag)
	{
		userspace_gui_first_draw_tag = 1;
		klog_smoke("USERSPACE_FB_TEXT_FIRST_DRAW_OK");
	}
}

void vt_userspace_handoff(void)
{
	static const char ready[] = "IR0 console ready\n";

	vt_clear(IR0_CONSOLE_COLOR_DEFAULT);

	if (console_use_framebuffer())
		klog_smoke("CONSOLE_BACKEND_FB_OK");
	else
		klog_smoke("CONSOLE_BACKEND_VGA_OK");

	klog_smoke("PRINTK_SERIAL_CONSOLE_FB_HANDOFF_OK");
	console_backend_write(ready, sizeof(ready) - 1,
			      IR0_CONSOLE_COLOR_DEFAULT);
	klog_smoke("CONSOLE_GUI_VISIBLE_OK");
}
