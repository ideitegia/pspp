/* con32s - emulates Windows console.
   Copyright (C) 1997, 1998 Free Software Foundation, Inc.
   Written by Ben Pfaff <blp@gnu.org>.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stdarg.h>

typedef struct line_struct line;
struct line_struct
  {
    line *next, *prev;		/* next and previous lines */
    char *text;			/* text */
    int len;			/* number of characters in text */
    int size;			/* maximum allocated size for text */
  };				/* line */

/* Pointer to tail end of text lines. */
static line *tail;

/* Console window created. */
static int inited = 0;

/* Console window title. */
static const char *title = _("Con32s Console Emulator by Ben Pfaff");

/* Important variables. */
HINSTANCE _hInstance;
HINSTANCE _hPrev;
LPSTR _cmdline;
int _nCmdShow;

/* Console window. */
HWND wnd;

/* Width, height of a single character in the console font. */
int cw, ch;

/* Width, height of console window in characters. */
int nw, nh;

/* Keyboard buffer. */
#define MAX_KBD_BUF 80		/* Maximum number of characters to buffer. */
char kbd[MAX_KBD_BUF];
char *hp, *tp;			/* Keyboard buffer head, tail. */

static void
outmsg (char *format,...)
{
  va_list args;
  char s[128];

  va_start (args, format);
  vsprintf (s, format, args);
  va_end (args);
  MessageBox (_hInstance, s, "Con32s",
	      MB_OK | MB_ICONHAND | MB_SYSTEMMODAL);
}

static void *
xmalloc (size_t size)
{
  void *vp;
  if (size == 0)
    return NULL;
  vp = malloc (size);
  if (!vp)
    {
      MessageBox (NULL, _("xmalloc(): out of memory"), NULL, MB_OK);
      exit (EXIT_FAILURE);
    }
  return vp;
}

static void *
xrealloc (void *ptr, size_t size)
{
  void *vp;
  if (!size)
    {
      if (ptr)
	free (ptr);
      return NULL;
    }
  if (ptr)
    vp = realloc (ptr, size);
  else
    vp = malloc (size);
  if (!vp)
    {
      MessageBox (NULL, _("xrealloc(): out of memory"), NULL, MB_OK);
      exit (EXIT_FAILURE);
    }
  return vp;
}

void _blp_console_init (void);
void _blp_console_yield (void);
void _blp_console_paint (void);
void find_console_top (line ** top);
void find_console_bottom (int *x, int *y, line ** bottom);

static void
writechar (int c)
{
  int x, y;
  line *bottom;

  static HDC dc;

  if (c == 10000)
    {
      if (dc)
	{
	  ReleaseDC (wnd, dc);
	  dc = 0;
	}
      return;
    }

  if (!tail)
    {
      tail = xmalloc (sizeof (line));
      tail->next = tail->prev = NULL;
      tail->text = NULL;
      tail->len = tail->size = 0;
    }

  switch (c)
    {
    case '\n':
      {
	tail->next = xmalloc (sizeof (line));
	tail->next->prev = tail;
	tail = tail->next;
	tail->next = NULL;
	tail->text = NULL;
	tail->len = tail->size = 0;
      }
      break;
    case '\r':
      break;
    case '\b':
      {
	find_console_bottom (&x, &y, &bottom);
	if (tail->len)
	  tail->len--;
	else
	  {
	    tail = tail->prev;
	    free (tail->next);
	    tail->next = NULL;
	  }

	if (x > 1)
	  {
	    if (!dc)
	      {
		dc = GetDC (wnd);
		SelectObject (dc, GetStockObject (ANSI_FIXED_FONT));
		assert (dc);
	      }
	    TextOut (dc, x * cw, y * ch, " ", 1);
	    return;
	  }
      }
      break;
    default:
      {
	if (tail->len + 1 > tail->size)
	  {
	    tail->size += 16;
	    tail->text = xrealloc (tail->text, tail->size);
	  }

	find_console_bottom (&x, &y, &bottom);
	tail->text[tail->len++] = c;
	if (y < nh)
	  {
	    if (!dc)
	      {
		dc = GetDC (wnd);
		SelectObject (dc, GetStockObject (ANSI_FIXED_FONT));
		assert (dc);
	      }
	    TextOut (dc, x * cw, y * ch, &tail->text[tail->len - 1], 1);
	    return;
	  }
      }
      break;
    }
  InvalidateRect (wnd, NULL, TRUE);
}

/* Writes LEN bytes from BUF to the fake console window. */
int
_blp_console_write (const void *buf, unsigned len)
{
  int i;

  if (!inited)
    _blp_console_init ();
  for (i = 0; i < len; i++)
    writechar (((char *) buf)[i]);
  writechar (10000);
  return len;
}

/* Reads one character from the fake console window.  A whole line
   is read at once, then spoon-fed to the runtime library. */
#if __BORLANDC__
#pragma argsused
#endif
int
_blp_console_read (const void *t1, unsigned t2)
{
  static char buf[1024];
  static int len;
  static int n;

  MSG msg;

  int c;

  if (!inited)
    _blp_console_init ();
  if (n < len)
    {
      *(char *) t1 = buf[n];
      n++;
      return 1;
    }

  printf ("_");
  len = n = 0;
  while (GetMessage ((LPMSG) & msg, NULL, 0, 0))
    {
      TranslateMessage ((LPMSG) & msg);
      DispatchMessage ((LPMSG) & msg);

      while (hp != tp)
	{
	  c = *(unsigned char *) tp;
	  if (++tp >= &kbd[MAX_KBD_BUF])
	    tp = kbd;
	  if ((c >= 32 && c < 128) || c == '\b' || c == '\r')
	    switch (c)
	      {
	      case '\b':
		if (len <= 0)
		  break;
		printf ("\b\b_");
		len--;
		break;
	      default:
		if (len >= 1022)
		  break;
		if (c == '\r')
		  {
		    buf[len++] = '\n';
		    printf ("\b\n");
		    *(char *) t1 = buf[n];
		    n++;
		    return 1;
		  }
		buf[len++] = c;
		printf ("\b%c_", c);
		break;
	      }
	}
    }
  len = 0;
  return 0;
}

LRESULT CALLBACK _export _blp_console_wndproc (HWND, UINT, WPARAM, LPARAM);

void
_blp_console_init (void)
{
  WNDCLASS wc;

  if (inited)
    return;
  inited = 1;
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = _blp_console_wndproc;
  wc.cbClsExtra = 0;
  wc.cbWndExtra = 0;
  wc.hInstance = (HINSTANCE) _hInstance;
  wc.hIcon = LoadIcon (NULL, IDI_APPLICATION);
  wc.hCursor = LoadCursor (NULL, IDC_ARROW);
  wc.hbrBackground = CreateSolidBrush (RGB (255, 255, 255));
  wc.lpszMenuName = NULL;
  wc.lpszClassName = "blp_console";
  if (!RegisterClass (&wc))
    {
      MessageBox ((HWND) 0, _("RegisterClass(): returned 0."),
		  "_blp_console_init()", MB_APPLMODAL | MB_OK);
      exit (EXIT_FAILURE);
    }

  wnd = CreateWindow ("blp_console", title, WS_OVERLAPPEDWINDOW,
		      CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		      CW_USEDEFAULT, NULL, (HMENU) 0, (HINSTANCE) _hInstance,
		      NULL);
  if (!wnd)
    {
      MessageBox ((HWND) 0, _("CreateWindow(): returned 0."),
		  "_blp_console_init()", MB_APPLMODAL | MB_OK);
      exit (EXIT_FAILURE);
    }

  ShowWindow (wnd, _nCmdShow);

  hp = tp = kbd;
}

LRESULT CALLBACK _export
_blp_console_wndproc (HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  char s[80];

  switch (msg)
    {
    case WM_CREATE:
      {
	HDC dc = GetDC (hwnd);
	TEXTMETRIC tm;
	int success;

	SelectObject (dc, GetStockObject (ANSI_FIXED_FONT));
	success = GetTextMetrics (dc, &tm);
	assert (success);
	cw = tm.tmMaxCharWidth;
	ch = tm.tmHeight;
	success = ReleaseDC (hwnd, dc);
	assert (success);
	return 0;
      }
    case WM_PAINT:
      _blp_console_paint ();
      return 0;
    case WM_CHAR:
      {
	if (hp + 1 != tp && (hp != &kbd[MAX_KBD_BUF - 1] || tp != kbd))
	  {
	    *hp++ = wp;
	    if (hp >= &kbd[MAX_KBD_BUF])
	      hp = kbd;
	  }
      }
      break;
    }
  return DefWindowProc (hwnd, msg, wp, lp);
}

static void
find_console_top (line ** top)
{
  int success;

  /* Line count. */
  int lc;

  /* Line iterator. */
  line *iter;

  /* Scratch line. */
  static line temp;

  /* Client rectangle. */
  RECT r;

  success = GetClientRect (wnd, &r);
  assert (success);
  nw = r.right / cw;
  if (nw < 1)
    nw = 1;
  nh = r.bottom / ch;
  if (nh < 1)
    nh = 1;

  /* Find the beginning of the text to display. */
  for (lc = 0, iter = tail; iter; iter = iter->prev)
    {
      if (!iter->len)
	lc++;
      else
	lc += (iter->len / nw) + (iter->len % nw > 0);
      if (lc >= nh || !iter->prev)
	break;
    }
  if (lc > nh)
    {
      temp = *iter;
      temp.text += nw * (lc - nh);
      temp.len -= nw * (lc - nh);
      *top = &temp;
    }
  else
    *top = iter;
}

static void
find_console_bottom (int *x, int *y, line ** bottom)
{
  find_console_top (bottom);
  *x = *y = 0;
  if (!*bottom)
    return;
  while (1)
    {
      if ((*bottom)->len == 0)
	(*y)++;
      else
	(*y) += ((*bottom)->len / nw) + ((*bottom)->len % nw > 0);
      if (!(*bottom)->next)
	break;
      *bottom = (*bottom)->next;
    }
  *x = (*bottom)->len % nw;
  (*y)--;
}

void
_blp_console_paint (void)
{
  PAINTSTRUCT ps;
  HDC dc;

  /* Current screen location. */
  int x, y;

  /* Current line. */
  line *iter;

  dc = BeginPaint (wnd, &ps);
  assert (dc);

  find_console_top (&iter);

  /* Display the text. */
  SelectObject (dc, GetStockObject (ANSI_FIXED_FONT));
  SetTextColor (dc, RGB (0, 0, 0));
  for (y = 0; iter; iter = iter->next)
    {
      if (!iter->len)
	{
	  y += ch;
	  continue;
	}
      for (x = 0; x < iter->len; x += nw)
	{
	  TextOut (dc, 0, y, &iter->text[x],
		   iter->len - x > nw ? nw : iter->len - x);
	  y += ch;
	}
    }

  EndPaint (wnd, &ps);
}

int main (int argc, char *argv[], char *env[]);

#if __BORLANDC__
#pragma argsused
#endif
int CALLBACK
WinMain (HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int nCmdShow)
{
  int result;
  MSG msg;

  char *pgmname = "PSPP";

  _hInstance = inst;
  _hPrev = prev;
  _cmdline = cmdline;
  _nCmdShow = nCmdShow;

  result = main (1, &pgmname, NULL);

  return result;
}
