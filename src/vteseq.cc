/*
 * Copyright © 2001-2004 Red Hat, Inc.
 * Copyright © 2015 David Herrmann <dh.herrmann@gmail.com>
 * Copyright © 2008-2018 Christian Persch
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "config.h"

#include <search.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#ifdef HAVE_SYS_SYSLIMITS_H
#include <sys/syslimits.h>
#endif

#include <glib.h>

#include <vte/vte.h>
#include "vteinternal.hh"
#include "vtegtk.hh"
#include "caps.hh"
#include "debug.h"

#define BEL_C0 "\007"
#define ST_C0 _VTE_CAP_ST

#include <algorithm>

using namespace std::literals;

void
vte::parser::Sequence::print() const
{
#ifdef VTE_DEBUG
        auto c = m_seq != nullptr ? terminator() : 0;
        char c_buf[7];
        g_snprintf(c_buf, sizeof(c_buf), "%lc", c);
        g_printerr("%s:%s [%s]", type_string(), command_string(),
                   g_unichar_isprint(c) ? c_buf : _vte_debug_sequence_to_string(c_buf, -1));
        if (m_seq != nullptr && m_seq->n_args > 0) {
                g_printerr("[ ");
                for (unsigned int i = 0; i < m_seq->n_args; i++) {
                        if (i > 0)
                                g_print(", ");
                        g_printerr("%d", vte_seq_arg_value(m_seq->args[i]));
                }
                g_printerr(" ]");
        }
        if (m_seq->type == VTE_SEQ_OSC) {
                char* str = string_param();
                g_printerr(" \"%s\"", str);
                g_free(str);
        }
        g_printerr("\n");
#endif
}

char const*
vte::parser::Sequence::type_string() const
{
        if (G_UNLIKELY(m_seq == nullptr))
                return "(nil)";

        switch (type()) {
        case VTE_SEQ_NONE:    return "NONE";
        case VTE_SEQ_IGNORE:  return "IGNORE";
        case VTE_SEQ_GRAPHIC: return "GRAPHIC";
        case VTE_SEQ_CONTROL: return "CONTROL";
        case VTE_SEQ_ESCAPE:  return "ESCAPE";
        case VTE_SEQ_CSI:     return "CSI";
        case VTE_SEQ_DCS:     return "DCS";
        case VTE_SEQ_OSC:     return "OSC";
        default:
                g_assert(false);
                return nullptr;
        }
}

char const*
vte::parser::Sequence::command_string() const
{
        if (G_UNLIKELY(m_seq == nullptr))
                return "(nil)";

        switch (command()) {
#define _VTE_CMD(cmd) case VTE_CMD_##cmd: return #cmd;
#include "parser-cmd.hh"
#undef _VTE_CMD
        default:
                static char buf[32];
                snprintf(buf, sizeof(buf), "UNKOWN(%u)", command());
                return buf;
        }
}

// FIXMEchpe optimise this
std::string
vte::parser::Sequence::string_utf8() const noexcept
{
        std::string str;

        size_t len;
        auto buf = vte_seq_string_get(&m_seq->arg_str, &len);

        char u[6];
        for (size_t i = 0; i < len; ++i) {
                auto ulen = g_unichar_to_utf8(buf[i], u);
                str.append((char const*)u, ulen);
        }

        return str;
}

/* A couple are duplicated from vte.c, to keep them static... */

/* Check how long a string of unichars is.  Slow version. */
static gsize
vte_unichar_strlen(gunichar const* c)
{
	gsize i;
	for (i = 0; c[i] != 0; i++) ;
	return i;
}

/* Convert a wide character string to a multibyte string */
/* Simplified from glib's g_ucs4_to_utf8() to simply allocate the maximum
 * length instead of walking the input twice.
 */
char*
vte::parser::Sequence::ucs4_to_utf8(gunichar const* str,
                                    ssize_t len) const
{
        if (len < 0)
                len = vte_unichar_strlen(str);
        auto outlen = (len * VTE_UTF8_BPC) + 1;

        auto result = (char*)g_try_malloc(outlen);
        if (result == nullptr)
                return nullptr;

        auto end = str + len;
        auto p = result;
        for (auto i = str; i < end; i++)
                p += g_unichar_to_utf8(*i, p);
        *p = '\0';

        return result;
}

/* Emit a "bell" signal. */
void
VteTerminalPrivate::emit_bell()
{
        _vte_debug_print(VTE_DEBUG_SIGNALS, "Emitting `bell'.\n");
        g_signal_emit(m_terminal, signals[SIGNAL_BELL], 0);
}


/* Emit a "deiconify-window" signal. */
void
VteTerminalPrivate::emit_deiconify_window()
{
        _vte_debug_print(VTE_DEBUG_SIGNALS, "Emitting `deiconify-window'.\n");
        g_signal_emit(m_terminal, signals[SIGNAL_DEICONIFY_WINDOW], 0);
}

/* Emit a "iconify-window" signal. */
void
VteTerminalPrivate::emit_iconify_window()
{
        _vte_debug_print(VTE_DEBUG_SIGNALS, "Emitting `iconify-window'.\n");
        g_signal_emit(m_terminal, signals[SIGNAL_ICONIFY_WINDOW], 0);
}

/* Emit a "raise-window" signal. */
void
VteTerminalPrivate::emit_raise_window()
{
        _vte_debug_print(VTE_DEBUG_SIGNALS, "Emitting `raise-window'.\n");
        g_signal_emit(m_terminal, signals[SIGNAL_RAISE_WINDOW], 0);
}

/* Emit a "lower-window" signal. */
void
VteTerminalPrivate::emit_lower_window()
{
        _vte_debug_print(VTE_DEBUG_SIGNALS, "Emitting `lower-window'.\n");
        g_signal_emit(m_terminal, signals[SIGNAL_LOWER_WINDOW], 0);
}

/* Emit a "maximize-window" signal. */
void
VteTerminalPrivate::emit_maximize_window()
{
        _vte_debug_print(VTE_DEBUG_SIGNALS, "Emitting `maximize-window'.\n");
        g_signal_emit(m_terminal, signals[SIGNAL_MAXIMIZE_WINDOW], 0);
}

/* Emit a "refresh-window" signal. */
void
VteTerminalPrivate::emit_refresh_window()
{
        _vte_debug_print(VTE_DEBUG_SIGNALS, "Emitting `refresh-window'.\n");
        g_signal_emit(m_terminal, signals[SIGNAL_REFRESH_WINDOW], 0);
}

/* Emit a "restore-window" signal. */
void
VteTerminalPrivate::emit_restore_window()
{
        _vte_debug_print(VTE_DEBUG_SIGNALS, "Emitting `restore-window'.\n");
        g_signal_emit(m_terminal, signals[SIGNAL_RESTORE_WINDOW], 0);
}

/* Emit a "move-window" signal.  (Pixels.) */
void
VteTerminalPrivate::emit_move_window(guint x,
                                     guint y)
{
        _vte_debug_print(VTE_DEBUG_SIGNALS, "Emitting `move-window'.\n");
        g_signal_emit(m_terminal, signals[SIGNAL_MOVE_WINDOW], 0, x, y);
}

/* Emit a "resize-window" signal.  (Grid size.) */
void
VteTerminalPrivate::emit_resize_window(guint columns,
                                       guint rows)
{
        _vte_debug_print(VTE_DEBUG_SIGNALS, "Emitting `resize-window'.\n");
        g_signal_emit(m_terminal, signals[SIGNAL_RESIZE_WINDOW], 0, columns, rows);
}

/* Some common functions */

/* In Xterm, upon printing a character in the last column the cursor doesn't
 * advance.  It's special cased that printing the following letter will first
 * wrap to the next row.
 *
 * As a rule of thumb, escape sequences that move the cursor (e.g. cursor up)
 * or immediately update the visible contents (e.g. clear in line) disable
 * this special mode, whereas escape sequences with no immediate visible
 * effect (e.g. color change) leave this special mode on.  There are
 * exceptions of course (e.g. scroll up).
 *
 * In VTE, a different technical approach is used.  The cursor is advanced to
 * the invisible column on the right, but it's set back to the visible
 * rightmost column whenever necessary (that is, before handling any of the
 * sequences that disable the special cased mode in xterm).  (Bug 731155.)
 */
void
VteTerminalPrivate::ensure_cursor_is_onscreen()
{
        if (G_UNLIKELY (m_screen->cursor.col >= m_column_count))
                m_screen->cursor.col = m_column_count - 1;
}

void
VteTerminalPrivate::home_cursor()
{
        set_cursor_coords(0, 0);
}

void
VteTerminalPrivate::clear_screen()
{
        auto row = m_screen->cursor.row - m_screen->insert_delta;
        auto initial = _vte_ring_next(m_screen->row_data);
	/* Add a new screen's worth of rows. */
        for (auto i = 0; i < m_row_count; i++)
                ring_append(true);
	/* Move the cursor and insertion delta to the first line in the
	 * newly-cleared area and scroll if need be. */
        m_screen->insert_delta = initial;
        m_screen->cursor.row = row + m_screen->insert_delta;
        adjust_adjustments();
	/* Redraw everything. */
        invalidate_all();
	/* We've modified the display.  Make a note of it. */
        m_text_deleted_flag = TRUE;
}

/* Clear the current line. */
void
VteTerminalPrivate::clear_current_line()
{
	VteRowData *rowdata;

	/* If the cursor is actually on the screen, clear data in the row
	 * which corresponds to the cursor. */
        if (_vte_ring_next(m_screen->row_data) > m_screen->cursor.row) {
		/* Get the data for the row which the cursor points to. */
                rowdata = _vte_ring_index_writable(m_screen->row_data, m_screen->cursor.row);
		g_assert(rowdata != NULL);
		/* Remove it. */
		_vte_row_data_shrink (rowdata, 0);
		/* Add enough cells to the end of the line to fill out the row. */
                _vte_row_data_fill (rowdata, &m_fill_defaults, m_column_count);
		rowdata->attr.soft_wrapped = 0;
		/* Repaint this row. */
		invalidate_cells(0, m_column_count,
                                 m_screen->cursor.row, 1);
	}

	/* We've modified the display.  Make a note of it. */
        m_text_deleted_flag = TRUE;
}

/* Clear above the current line. */
void
VteTerminalPrivate::clear_above_current()
{
	/* If the cursor is actually on the screen, clear data in the row
	 * which corresponds to the cursor. */
        for (auto i = m_screen->insert_delta; i < m_screen->cursor.row; i++) {
                if (_vte_ring_next(m_screen->row_data) > i) {
			/* Get the data for the row we're erasing. */
                        auto rowdata = _vte_ring_index_writable(m_screen->row_data, i);
			g_assert(rowdata != NULL);
			/* Remove it. */
			_vte_row_data_shrink (rowdata, 0);
			/* Add new cells until we fill the row. */
                        _vte_row_data_fill (rowdata, &m_fill_defaults, m_column_count);
			rowdata->attr.soft_wrapped = 0;
			/* Repaint the row. */
			invalidate_cells(0, m_column_count, i, 1);
		}
	}
	/* We've modified the display.  Make a note of it. */
        m_text_deleted_flag = TRUE;
}

/* Scroll the text, but don't move the cursor.  Negative = up, positive = down. */
void
VteTerminalPrivate::scroll_text(vte::grid::row_t scroll_amount)
{
        vte::grid::row_t start, end;
        if (m_scrolling_restricted) {
                start = m_screen->insert_delta + m_scrolling_region.start;
                end = m_screen->insert_delta + m_scrolling_region.end;
	} else {
                start = m_screen->insert_delta;
                end = start + m_row_count - 1;
	}

        while (_vte_ring_next(m_screen->row_data) <= end)
                ring_append(false);

	if (scroll_amount > 0) {
		for (auto i = 0; i < scroll_amount; i++) {
                        ring_remove(end);
                        ring_insert(start, true);
		}
	} else {
		for (auto i = 0; i < -scroll_amount; i++) {
                        ring_remove(start);
                        ring_insert(end, true);
		}
	}

	/* Update the display. */
        scroll_region(start, end - start + 1, scroll_amount);

	/* Adjust the scrollbars if necessary. */
        adjust_adjustments();

	/* We've modified the display.  Make a note of it. */
        m_text_inserted_flag = TRUE;
        m_text_deleted_flag = TRUE;
}

 void
VteTerminalPrivate::restore_cursor()
{
        restore_cursor(m_screen);
        ensure_cursor_is_onscreen();
}

void
VteTerminalPrivate::save_cursor()
{
        save_cursor(m_screen);
}

/* Switch to normal screen. */
void
VteTerminalPrivate::switch_normal_screen()
{
        switch_screen(&m_normal_screen);
}

void
VteTerminalPrivate::switch_screen(VteScreen *new_screen)
{
        /* if (new_screen == m_screen) return; ? */

        /* The two screens use different hyperlink pools, so carrying on the idx
         * wouldn't make sense and could lead to crashes.
         * Ideally we'd carry the target URI itself, but I'm just lazy.
         * Also, run a GC before we switch away from that screen. */
        m_hyperlink_hover_idx = _vte_ring_get_hyperlink_at_position(m_screen->row_data, -1, -1, true, NULL);
        g_assert (m_hyperlink_hover_idx == 0);
        m_hyperlink_hover_uri = NULL;
        emit_hyperlink_hover_uri_changed(NULL);  /* FIXME only emit if really changed */
        m_defaults.attr.hyperlink_idx = _vte_ring_get_hyperlink_idx(m_screen->row_data, NULL);
        g_assert (m_defaults.attr.hyperlink_idx == 0);

        /* cursor.row includes insert_delta, adjust accordingly */
        auto cr = m_screen->cursor.row - m_screen->insert_delta;
        m_screen = new_screen;
        m_screen->cursor.row = cr + m_screen->insert_delta;

        /* Make sure the ring is large enough */
        ensure_row();
}

/* Switch to alternate screen. */
void
VteTerminalPrivate::switch_alternate_screen()
{
        switch_screen(&m_alternate_screen);
}

void
VteTerminalPrivate::set_mode_ecma(vte::parser::Sequence const& seq,
                                  bool set) noexcept
{
        auto const n_params = seq.size();
        for (unsigned int i = 0; i < n_params; i = seq.next(i)) {
                auto const param = seq.collect1(i);
                auto const mode = m_modes_ecma.mode_from_param(param);

                _vte_debug_print(VTE_DEBUG_MODES,
                                 "Mode %d (%s) %s\n",
                                 param, m_modes_ecma.mode_to_cstring(mode),
                                 set ? "set" : "reset");

                if (mode < 0)
                        continue;

                m_modes_ecma.set(mode, set);
        }
}

void
VteTerminalPrivate::update_mouse_protocol() noexcept
{
        if (m_modes_private.XTERM_MOUSE_ANY_EVENT())
                m_mouse_tracking_mode = MOUSE_TRACKING_ALL_MOTION_TRACKING;
        else if (m_modes_private.XTERM_MOUSE_BUTTON_EVENT())
                m_mouse_tracking_mode = MOUSE_TRACKING_CELL_MOTION_TRACKING;
        else if (m_modes_private.XTERM_MOUSE_VT220_HIGHLIGHT())
                m_mouse_tracking_mode = MOUSE_TRACKING_HILITE_TRACKING;
        else if (m_modes_private.XTERM_MOUSE_VT220())
                m_mouse_tracking_mode = MOUSE_TRACKING_SEND_XY_ON_BUTTON;
        else if (m_modes_private.XTERM_MOUSE_X10())
                m_mouse_tracking_mode = MOUSE_TRACKING_SEND_XY_ON_CLICK;
        else
                m_mouse_tracking_mode = MOUSE_TRACKING_NONE;

        m_mouse_smooth_scroll_delta = 0.0;

        /* Mouse pointer might change */
        apply_mouse_cursor();

        _vte_debug_print(VTE_DEBUG_MODES,
                         "Mouse protocol is now %d\n", m_mouse_tracking_mode);
}

void
VteTerminalPrivate::set_mode_private(int mode,
                                     bool set) noexcept
{
        /* Pre actions */
        switch (mode) {
        default:
                break;
        }

        m_modes_private.set(mode, set);

        /* Post actions */
        switch (mode) {
        case vte::terminal::modes::Private::eDEC_132_COLUMN:
                /* DECCOLM: set/reset to 132/80 columns mode, clear screen and cursor home */
                // FIXMEchpe don't do clear screen if DECNCSM is set
                if (m_modes_private.XTERM_DECCOLM()) {
                        emit_resize_window(set ? 132 : 80, m_row_count);
                        clear_screen();
                        home_cursor();
                }
                break;

        case vte::terminal::modes::Private::eDEC_REVERSE_IMAGE:
                invalidate_all();
                break;

        case vte::terminal::modes::Private::eDEC_ORIGIN:
                /* Reposition the cursor in its new home position. */
                home_cursor();
                break;

        case vte::terminal::modes::Private::eDEC_TEXT_CURSOR:
                /* No need to invalidate the cursor here, this is done
                 * in process_incoming().
                 */
                break;

        case vte::terminal::modes::Private::eXTERM_ALTBUF:
                /* [[fallthrough]]; */
        case vte::terminal::modes::Private::eXTERM_OPT_ALTBUF:
                /* [[fallthrough]]; */
        case vte::terminal::modes::Private::eXTERM_OPT_ALTBUF_SAVE_CURSOR:
                if (set) {
                        if (mode == vte::terminal::modes::Private::eXTERM_OPT_ALTBUF_SAVE_CURSOR)
                                save_cursor();

                        switch_alternate_screen();

                        /* Clear the alternate screen */
                        if (mode == vte::terminal::modes::Private::eXTERM_OPT_ALTBUF_SAVE_CURSOR)
                                clear_screen();
                } else {
                        if (mode == vte::terminal::modes::Private::eXTERM_OPT_ALTBUF &&
                            m_screen == &m_alternate_screen)
                                clear_screen();

                        switch_normal_screen();

                        if (mode == vte::terminal::modes::Private::eXTERM_OPT_ALTBUF_SAVE_CURSOR)
                                restore_cursor();
                }

                /* Reset scrollbars and repaint everything. */
                gtk_adjustment_set_value(m_vadjustment,
                                         m_screen->scroll_delta);
                set_scrollback_lines(m_scrollback_lines);
                queue_contents_changed();
                invalidate_all();
                break;

        case vte::terminal::modes::Private::eXTERM_SAVE_CURSOR:
                if (set)
                        save_cursor();
                else
                        restore_cursor();
                break;

        case vte::terminal::modes::Private::eXTERM_MOUSE_X10:
        case vte::terminal::modes::Private::eXTERM_MOUSE_VT220:
        case vte::terminal::modes::Private::eXTERM_MOUSE_VT220_HIGHLIGHT:
        case vte::terminal::modes::Private::eXTERM_MOUSE_BUTTON_EVENT:
        case vte::terminal::modes::Private::eXTERM_MOUSE_ANY_EVENT:
        case vte::terminal::modes::Private::eXTERM_MOUSE_EXT:
        case vte::terminal::modes::Private::eXTERM_MOUSE_EXT_SGR:
        case vte::terminal::modes::Private::eURXVT_MOUSE_EXT:
                update_mouse_protocol();
                break;

        case vte::terminal::modes::Private::eXTERM_FOCUS:
                if (set)
                        feed_focus_event_initial();
                break;

        default:
                break;
        }
}

void
VteTerminalPrivate::set_mode_private(vte::parser::Sequence const& seq,
                                     bool set) noexcept
{
        auto const n_params = seq.size();
        for (unsigned int i = 0; i < n_params; i = seq.next(i)) {
                auto const param = seq.collect1(i);
                auto const mode = m_modes_private.mode_from_param(param);

                _vte_debug_print(VTE_DEBUG_MODES,
                                 "Private mode %d (%s) %s\n",
                                 param, m_modes_private.mode_to_cstring(mode),
                                 set ? "set" : "reset");

                if (mode < 0)
                        continue;

                set_mode_private(mode, set);
        }
}

void
VteTerminalPrivate::save_mode_private(vte::parser::Sequence const& seq,
                                      bool save) noexcept
{
        auto const n_params = seq.size();
        for (unsigned int i = 0; i < n_params; i = seq.next(i)) {
                auto const param = seq.collect1(i);
                auto const mode = m_modes_private.mode_from_param(param);

                if (mode < 0) {
                        _vte_debug_print(VTE_DEBUG_MODES,
                                         "Saving private mode %d (%s)\n",
                                         param, m_modes_private.mode_to_cstring(mode));
                        continue;
                }

                if (save) {
                        _vte_debug_print(VTE_DEBUG_MODES,
                                         "Saving private mode %d (%s) is %s\n",
                                         param, m_modes_private.mode_to_cstring(mode),
                                         m_modes_private.get(mode) ? "set" : "reset");

                        m_modes_private.push_saved(mode);
                } else {
                        bool const set = m_modes_private.pop_saved(mode);

                        _vte_debug_print(VTE_DEBUG_MODES,
                                         "Restoring private mode %d (%s) to %s\n",
                                         param, m_modes_private.mode_to_cstring(mode),
                                         set ? "set" : "reset");

                        set_mode_private(mode, set);
                }
        }
}

void
VteTerminalPrivate::set_character_replacement(unsigned slot)
{
        g_assert(slot < G_N_ELEMENTS(m_character_replacements));
        m_character_replacement = &m_character_replacements[slot];
}

/* Clear from the cursor position (inclusive!) to the beginning of the line. */
void
VteTerminalPrivate::clear_to_bol()
{
        ensure_cursor_is_onscreen();

	/* Get the data for the row which the cursor points to. */
	auto rowdata = ensure_row();
        /* Clean up Tab/CJK fragments. */
        cleanup_fragments(0, m_screen->cursor.col + 1);
	/* Clear the data up to the current column with the default
	 * attributes.  If there is no such character cell, we need
	 * to add one. */
        vte::grid::column_t i;
        for (i = 0; i <= m_screen->cursor.col; i++) {
                if (i < (glong) _vte_row_data_length (rowdata)) {
			/* Muck with the cell in this location. */
                        auto pcell = _vte_row_data_get_writable(rowdata, i);
                        *pcell = m_color_defaults;
		} else {
			/* Add new cells until we have one here. */
                        _vte_row_data_append (rowdata, &m_color_defaults);
		}
	}
	/* Repaint this row. */
        invalidate_cells(0, m_screen->cursor.col+1,
                         m_screen->cursor.row, 1);

	/* We've modified the display.  Make a note of it. */
        m_text_deleted_flag = TRUE;
}

/* Clear to the right of the cursor and below the current line. */
void
VteTerminalPrivate::clear_below_current()
{
        ensure_cursor_is_onscreen();

	/* If the cursor is actually on the screen, clear the rest of the
	 * row the cursor is on and all of the rows below the cursor. */
        VteRowData *rowdata;
        auto i = m_screen->cursor.row;
	if (i < _vte_ring_next(m_screen->row_data)) {
		/* Get the data for the row we're clipping. */
                rowdata = _vte_ring_index_writable(m_screen->row_data, i);
                /* Clean up Tab/CJK fragments. */
                if ((glong) _vte_row_data_length(rowdata) > m_screen->cursor.col)
                        cleanup_fragments(m_screen->cursor.col, _vte_row_data_length(rowdata));
		/* Clear everything to the right of the cursor. */
		if (rowdata)
                        _vte_row_data_shrink(rowdata, m_screen->cursor.col);
	}
	/* Now for the rest of the lines. */
        for (i = m_screen->cursor.row + 1;
	     i < _vte_ring_next(m_screen->row_data);
	     i++) {
		/* Get the data for the row we're removing. */
		rowdata = _vte_ring_index_writable(m_screen->row_data, i);
		/* Remove it. */
		if (rowdata)
			_vte_row_data_shrink (rowdata, 0);
	}
	/* Now fill the cleared areas. */
        bool const not_default_bg = (m_fill_defaults.attr.back() != VTE_DEFAULT_BG);

        for (i = m_screen->cursor.row;
	     i < m_screen->insert_delta + m_row_count;
	     i++) {
		/* Retrieve the row's data, creating it if necessary. */
		if (_vte_ring_contains(m_screen->row_data, i)) {
			rowdata = _vte_ring_index_writable (m_screen->row_data, i);
			g_assert(rowdata != NULL);
		} else {
			rowdata = ring_append(false);
		}
		/* Pad out the row. */
                if (not_default_bg) {
                        _vte_row_data_fill(rowdata, &m_fill_defaults, m_column_count);
		}
		rowdata->attr.soft_wrapped = 0;
		/* Repaint this row. */
		invalidate_cells(0, m_column_count,
                                 i, 1);
	}

	/* We've modified the display.  Make a note of it. */
	m_text_deleted_flag = TRUE;
}

/* Clear from the cursor position to the end of the line. */
void
VteTerminalPrivate::clear_to_eol()
{
	/* If we were to strictly emulate xterm, we'd ensure the cursor is onscreen.
	 * But due to https://bugzilla.gnome.org/show_bug.cgi?id=740789 we intentionally
	 * deviate and do instead what konsole does. This way emitting a \e[K doesn't
	 * influence the text flow, and serves as a perfect workaround against a new line
	 * getting painted with the active background color (except for a possible flicker).
	 */
	/* ensure_cursor_is_onscreen(); */

	/* Get the data for the row which the cursor points to. */
        auto rowdata = ensure_row();
	g_assert(rowdata != NULL);
        if ((glong) _vte_row_data_length(rowdata) > m_screen->cursor.col) {
                /* Clean up Tab/CJK fragments. */
                cleanup_fragments(m_screen->cursor.col, _vte_row_data_length(rowdata));
                /* Remove the data at the end of the array until the current column
                 * is the end of the array. */
                _vte_row_data_shrink(rowdata, m_screen->cursor.col);
		/* We've modified the display.  Make a note of it. */
		m_text_deleted_flag = TRUE;
	}
        bool const not_default_bg = (m_fill_defaults.attr.back() != VTE_DEFAULT_BG);

        if (not_default_bg) {
		/* Add enough cells to fill out the row. */
                _vte_row_data_fill(rowdata, &m_fill_defaults, m_column_count);
	}
	rowdata->attr.soft_wrapped = 0;
	/* Repaint this row. */
	invalidate_cells(m_screen->cursor.col, m_column_count - m_screen->cursor.col,
                         m_screen->cursor.row, 1);
}

/*
 * VteTerminalPrivate::set_cursor_column:
 * @col: the column. 0-based from 0 to m_column_count - 1
 *
 * Sets the cursor column to @col, clamped to the range 0..m_column_count-1.
 */
void
VteTerminalPrivate::set_cursor_column(vte::grid::column_t col)
{
	_vte_debug_print(VTE_DEBUG_PARSER,
                         "Moving cursor to column %ld.\n", col);
        m_screen->cursor.col = CLAMP(col, 0, m_column_count - 1);
}

void
VteTerminalPrivate::set_cursor_column1(vte::grid::column_t col)
{
        set_cursor_column(col - 1);
}

/*
 * VteTerminalPrivate::set_cursor_row:
 * @row: the row. 0-based and relative to the scrolling region
 *
 * Sets the cursor row to @row. @row is relative to the scrolling region
 * (0 if restricted scrolling is off).
 */
void
VteTerminalPrivate::set_cursor_row(vte::grid::row_t row)
{
        vte::grid::row_t start_row, end_row;
        if (m_modes_private.DEC_ORIGIN() &&
            m_scrolling_restricted) {
                start_row = m_scrolling_region.start;
                end_row = m_scrolling_region.end;
        } else {
                start_row = 0;
                end_row = m_row_count - 1;
        }
        row += start_row;
        row = CLAMP(row, start_row, end_row);

        m_screen->cursor.row = row + m_screen->insert_delta;
}

void
VteTerminalPrivate::set_cursor_row1(vte::grid::row_t row)
{
        set_cursor_row(row - 1);
}

/*
 * VteTerminalPrivate::get_cursor_row:
 *
 * Returns: the relative cursor row, 0-based and relative to the scrolling region
 * if set (regardless of origin mode).
 */
vte::grid::row_t
VteTerminalPrivate::get_cursor_row() const
{
        auto row = m_screen->cursor.row - m_screen->insert_delta;
        /* Note that we do NOT check DEC_ORIGIN mode here! */
        if (m_scrolling_restricted) {
                row -= m_scrolling_region.start;
        }
        return row;
}

vte::grid::column_t
VteTerminalPrivate::get_cursor_column() const
{
        return m_screen->cursor.col;
}

/*
 * VteTerminalPrivate::set_cursor_coords:
 * @row: the row. 0-based and relative to the scrolling region
 * @col: the column. 0-based from 0 to m_column_count - 1
 *
 * Sets the cursor row to @row. @row is relative to the scrolling region
 * (0 if restricted scrolling is off).
 *
 * Sets the cursor column to @col, clamped to the range 0..m_column_count-1.
 */
void
VteTerminalPrivate::set_cursor_coords(vte::grid::row_t row,
                                      vte::grid::column_t column)
{
        set_cursor_column(column);
        set_cursor_row(row);
}

void
VteTerminalPrivate::set_cursor_coords1(vte::grid::row_t row,
                                      vte::grid::column_t column)
{
        set_cursor_column1(column);
        set_cursor_row1(row);
}

/* Delete a character at the current cursor position. */
void
VteTerminalPrivate::delete_character()
{
	VteRowData *rowdata;
	long col;

        ensure_cursor_is_onscreen();

        if (_vte_ring_next(m_screen->row_data) > m_screen->cursor.row) {
		long len;
		/* Get the data for the row which the cursor points to. */
                rowdata = _vte_ring_index_writable(m_screen->row_data, m_screen->cursor.row);
		g_assert(rowdata != NULL);
                col = m_screen->cursor.col;
		len = _vte_row_data_length (rowdata);
		/* Remove the column. */
		if (col < len) {
                        /* Clean up Tab/CJK fragments. */
                        cleanup_fragments(col, col + 1);
			_vte_row_data_remove (rowdata, col);
                        bool const not_default_bg = (m_fill_defaults.attr.back() != VTE_DEFAULT_BG);

                        if (not_default_bg) {
                                _vte_row_data_fill(rowdata, &m_fill_defaults, m_column_count);
                                len = m_column_count;
			}
                        rowdata->attr.soft_wrapped = 0;
			/* Repaint this row. */
                        invalidate_cells(col, len - col,
                                         m_screen->cursor.row, 1);
		}
	}

	/* We've modified the display.  Make a note of it. */
        m_text_deleted_flag = TRUE;
}

void
VteTerminalPrivate::move_cursor_down(vte::grid::row_t rows)
{
        rows = CLAMP(rows, 1, m_row_count);

        // FIXMEchpe why not do this afterwards?
        ensure_cursor_is_onscreen();

        vte::grid::row_t end;
        // FIXMEchpe why not check DEC_ORIGIN here?
        if (m_scrolling_restricted) {
                end = m_screen->insert_delta + m_scrolling_region.end;
	} else {
                end = m_screen->insert_delta + m_row_count - 1;
	}

        m_screen->cursor.row = MIN(m_screen->cursor.row + rows, end);
}

void
VteTerminalPrivate::erase_characters(long count)
{
	VteCell *cell;
	long col, i;

        ensure_cursor_is_onscreen();

	/* Clear out the given number of characters. */
	auto rowdata = ensure_row();
        if (_vte_ring_next(m_screen->row_data) > m_screen->cursor.row) {
		g_assert(rowdata != NULL);
                /* Clean up Tab/CJK fragments. */
                cleanup_fragments(m_screen->cursor.col, m_screen->cursor.col + count);
		/* Write over the characters.  (If there aren't enough, we'll
		 * need to create them.) */
		for (i = 0; i < count; i++) {
                        col = m_screen->cursor.col + i;
			if (col >= 0) {
				if (col < (glong) _vte_row_data_length (rowdata)) {
					/* Replace this cell with the current
					 * defaults. */
					cell = _vte_row_data_get_writable (rowdata, col);
                                        *cell = m_color_defaults;
				} else {
					/* Add new cells until we have one here. */
                                        _vte_row_data_fill (rowdata, &m_color_defaults, col + 1);
				}
			}
		}
		/* Repaint this row. */
                invalidate_cells(m_screen->cursor.col, count,
                                 m_screen->cursor.row, 1);
	}

	/* We've modified the display.  Make a note of it. */
        m_text_deleted_flag = TRUE;
}

/* Insert a blank character. */
void
VteTerminalPrivate::insert_blank_character()
{
        ensure_cursor_is_onscreen();

        auto save = m_screen->cursor;
        insert_char(' ', true, true);
        m_screen->cursor = save;
}

void
VteTerminalPrivate::move_cursor_backward(vte::grid::column_t columns)
{
        ensure_cursor_is_onscreen();

        auto col = get_cursor_column();
        columns = CLAMP(columns, 1, col);
        set_cursor_column(col - columns);
}

void
VteTerminalPrivate::move_cursor_forward(vte::grid::column_t columns)
{
        columns = CLAMP(columns, 1, m_column_count);

        ensure_cursor_is_onscreen();

        /* The cursor can be further to the right, don't move in that case. */
        auto col = get_cursor_column();
        if (col < m_column_count) {
		/* There's room to move right. */
                set_cursor_column(col + columns);
	}
}

void
VteTerminalPrivate::line_feed()
{
        ensure_cursor_is_onscreen();
        cursor_down(true);
}

void
VteTerminalPrivate::move_cursor_tab_backward(int count)
{
        if (count == 0)
                return;

        auto const newcol = m_tabstops.get_previous(m_screen->cursor.col, count, 0);
        set_cursor_column(newcol);
}

void
VteTerminalPrivate::move_cursor_tab_forward(int count)
{
        if (count == 0)
                return;

        auto const col = m_screen->cursor.col;
	g_assert (col >= 0);

	/* Find the next tabstop, but don't go beyond the end of the line */
        auto const newcol = m_tabstops.get_next(col, count, m_column_count - 1);

	/* Make sure we don't move cursor back (see bug #340631) */
        // FIXMEchpe how could this happen!?
	if (col >= newcol)
                return;

        /* Smart tab handling: bug 353610
         *
         * If we currently don't have any cells in the space this
         * tab creates, we try to make the tab character copyable,
         * by appending a single tab char with lots of fragment
         * cells following it.
         *
         * Otherwise, just append empty cells that will show up
         * as a space each.
         */

        VteRowData *rowdata = ensure_row();
        auto const old_len = _vte_row_data_length (rowdata);
        _vte_row_data_fill (rowdata, &basic_cell, newcol);

        /* Insert smart tab if there's nothing in the line after
         * us, not even empty cells (with non-default background
         * color for example).
         *
         * Notable bugs here: 545924, 597242, 764330
         */
        if (col >= old_len && (newcol - col) <= VTE_TAB_WIDTH_MAX) {
                glong i;
                VteCell *cell = _vte_row_data_get_writable (rowdata, col);
                VteCell tab = *cell;
                tab.attr.set_columns(newcol - col);
                tab.c = '\t';
                /* Save tab char */
                *cell = tab;
                /* And adjust the fragments */
                for (i = col + 1; i < newcol; i++) {
                        cell = _vte_row_data_get_writable (rowdata, i);
                        cell->c = '\t';
                        cell->attr.set_columns(1);
                        cell->attr.set_fragment(true);
                }
        }

        invalidate_cells(m_screen->cursor.col, newcol - m_screen->cursor.col,
                         m_screen->cursor.row, 1);
        m_screen->cursor.col = newcol;
}

void
VteTerminalPrivate::move_cursor_up(vte::grid::row_t rows)
{
        // FIXMEchpe allow 0 as no-op?
        rows = CLAMP(rows, 1, m_row_count);

        //FIXMEchpe why not do this afterward?
        ensure_cursor_is_onscreen();

        vte::grid::row_t start;
        //FIXMEchpe why not check DEC_ORIGIN mode here?
        if (m_scrolling_restricted) {
                start = m_screen->insert_delta + m_scrolling_region.start;
	} else {
		start = m_screen->insert_delta;
	}

        m_screen->cursor.row = MAX(m_screen->cursor.row - rows, start);
}

/*
 * Parse parameters of SGR 38, 48 or 58, starting at @index within @seq.
 * Returns %true if @seq contained colour parameters at @index, or %false otherwise.
 * In each case, @idx is set to last consumed parameter,
 * and the colour is returned in @color.
 *
 * The format looks like:
 * - 256 color indexed palette:
 *   - ^[[38:5:INDEXm  (de jure standard: ITU-T T.416 / ISO/IEC 8613-6; we also allow and ignore further parameters)
 *   - ^[[38;5;INDEXm  (de facto standard, understood by probably all terminal emulators that support 256 colors)
 * - true colors:
 *   - ^[[38:2:[id]:RED:GREEN:BLUE[:...]m  (de jure standard: ITU-T T.416 / ISO/IEC 8613-6)
 *   - ^[[38:2:RED:GREEN:BLUEm             (common misinterpretation of the standard, FIXME: stop supporting it at some point)
 *   - ^[[38;2;RED;GREEN;BLUEm             (de facto standard, understood by probably all terminal emulators that support true colors)
 * See bugs 685759 and 791456 for details.
 */
template<unsigned int redbits, unsigned int greenbits, unsigned int bluebits>
bool
VteTerminalPrivate::seq_parse_sgr_color(vte::parser::Sequence const& seq,
                                        unsigned int &idx,
                                        uint32_t& color) const noexcept
{
        /* Note that we don't have to check if the index is after the end of
         * the parameters list, since dereferencing is safe and returns -1.
         */

        if (seq.param_nonfinal(idx)) {
                /* Colon version */
                switch (seq.param(++idx)) {
                case 2: {
                        auto const n = seq.next(idx) - idx;
                        if (n < 4)
                                return false;
                        if (n > 4) {
                                /* Consume a colourspace parameter; it must be default */
                                if (!seq.param_default(++idx))
                                        return false;
                        }

                        int red = seq.param(++idx);
                        int green = seq.param(++idx);
                        int blue = seq.param(++idx);
                        if ((red & 0xff) != red ||
                            (green & 0xff) != green ||
                            (blue & 0xff) != blue)
                                return false;

                        color = VTE_RGB_COLOR(redbits, greenbits, bluebits, red, green, blue);
                        return true;
                }
                case 5: {
                        auto const n = seq.next(idx) - idx;
                        if (n < 2)
                                return false;

                        int v = seq.param(++idx);
                        if (v < 0 || v >= 256)
                                return false;

                        color = (uint32_t)v;
                        return true;
                }
                }
        } else {
                /* Semicolon version */

                idx = seq.next(idx);
                switch (seq.param(idx)) {
                case 2: {
                        /* Consume 3 more parameters */
                        idx = seq.next(idx);
                        int red = seq.param(idx);
                        idx = seq.next(idx);
                        int green = seq.param(idx);
                        idx = seq.next(idx);
                        int blue = seq.param(idx);

                        if ((red & 0xff) != red ||
                            (green & 0xff) != green ||
                            (blue & 0xff) != blue)
                                return false;

                        color = VTE_RGB_COLOR(redbits, greenbits, bluebits, red, green, blue);
                        return true;
                }
                case 5: {
                        /* Consume 1 more parameter */
                        idx = seq.next(idx);
                        int v = seq.param(idx);

                        if ((v & 0xff) != v)
                                return false;

                        color = (uint32_t)v;
                        return true;
                }
                }
        }

        return false;
}

void
VteTerminalPrivate::erase_in_display(vte::parser::Sequence const& seq)
{
        /* We don't implement the protected attribute, so we can ignore selective:
         * bool selective = (seq.command() == VTE_CMD_DECSED);
         */

        switch (seq.collect1(0)) {
        case -1: /* default */
	case 0:
		/* Clear below the current line. */
                clear_below_current();
		break;
	case 1:
		/* Clear above the current line. */
                clear_above_current();
		/* Clear everything to the left of the cursor, too. */
		/* FIXME: vttest. */
                clear_to_bol();
		break;
	case 2:
		/* Clear the entire screen. */
                clear_screen();
		break;
        case 3:
                /* Drop the scrollback. */
                drop_scrollback();
                break;
	default:
		break;
	}
	/* We've modified the display.  Make a note of it. */
        m_text_deleted_flag = TRUE;
}

void
VteTerminalPrivate::erase_in_line(vte::parser::Sequence const& seq)
{
        /* We don't implement the protected attribute, so we can ignore selective:
         * bool selective = (seq.command() == VTE_CMD_DECSEL);
         */

        switch (seq.collect1(0)) {
        case -1: /* default */
	case 0:
		/* Clear to end of the line. */
                clear_to_eol();
		break;
	case 1:
		/* Clear to start of the line. */
                clear_to_bol();
		break;
	case 2:
		/* Clear the entire line. */
                clear_current_line();
		break;
	default:
		break;
	}
	/* We've modified the display.  Make a note of it. */
        m_text_deleted_flag = TRUE;
}

void
VteTerminalPrivate::insert_lines(vte::grid::row_t param)
{
        vte::grid::row_t end, i;

	/* Find the region we're messing with. */
        auto row = m_screen->cursor.row;
        if (m_scrolling_restricted) {
                end = m_screen->insert_delta + m_scrolling_region.end;
	} else {
                end = m_screen->insert_delta + m_row_count - 1;
	}

	/* Only allow to insert as many lines as there are between this row
         * and the end of the scrolling region. See bug #676090.
         */
        auto limit = end - row + 1;
        param = MIN (param, limit);

	for (i = 0; i < param; i++) {
		/* Clear a line off the end of the region and add one to the
		 * top of the region. */
                ring_remove(end);
                ring_insert(row, true);
	}
        m_screen->cursor.col = 0;
	/* Update the display. */
        scroll_region(row, end - row + 1, param);
	/* Adjust the scrollbars if necessary. */
        adjust_adjustments();
	/* We've modified the display.  Make a note of it. */
        m_text_inserted_flag = TRUE;
}

void
VteTerminalPrivate::delete_lines(vte::grid::row_t param)
{
        vte::grid::row_t end, i;

	/* Find the region we're messing with. */
        auto row = m_screen->cursor.row;
        if (m_scrolling_restricted) {
                end = m_screen->insert_delta + m_scrolling_region.end;
	} else {
                end = m_screen->insert_delta + m_row_count - 1;
	}

        /* Only allow to delete as many lines as there are between this row
         * and the end of the scrolling region. See bug #676090.
         */
        auto limit = end - row + 1;
        param = MIN (param, limit);

	/* Clear them from below the current cursor. */
	for (i = 0; i < param; i++) {
		/* Insert a line at the end of the region and remove one from
		 * the top of the region. */
                ring_remove(row);
                ring_insert(end, true);
	}
        m_screen->cursor.col = 0;
	/* Update the display. */
        scroll_region(row, end - row + 1, -param);
	/* Adjust the scrollbars if necessary. */
        adjust_adjustments();
	/* We've modified the display.  Make a note of it. */
        m_text_deleted_flag = TRUE;
}

void
VteTerminalPrivate::set_color(vte::parser::Sequence const& seq,
                              vte::parser::StringTokeniser::const_iterator& token,
                              vte::parser::StringTokeniser::const_iterator const& endtoken) noexcept
{
        bool any_changed = false;

        while (token != endtoken) {
                int value;
                bool has_value = token.number(value);

                if (++token == endtoken)
                        break;

                if (!has_value ||
                    value < 0 ||
                    value >= VTE_DEFAULT_FG) {
                        ++token;
                        continue;
                }

                auto const str = *token;

                if (str == "?"s) {
                        auto c = get_color(value);
                        g_assert_nonnull(c);

                        reply(seq, VTE_REPLY_OSC, {}, "4;%d;rgb:%04x/%04x/%04x",
                              value, c->red, c->green, c->blue);
                } else {
                        vte::color::rgb color;
                        if (color.parse(str.data())) {
                                set_color(value, VTE_COLOR_SOURCE_ESCAPE, color);
                                any_changed = true;
                        }
                }

                ++token;
        }

        /* emit the refresh as the palette has changed and previous
         * renders need to be updated. */
        if (any_changed)
                emit_refresh_window();
}

void
VteTerminalPrivate::set_special_color(vte::parser::Sequence const& seq,
                                      vte::parser::StringTokeniser::const_iterator& token,
                                      vte::parser::StringTokeniser::const_iterator const& endtoken,
                                      int index,
                                      int index_fallback,
                                      int osc)
{
        if (token == endtoken)
                return;

        auto const str = *token;
        if (str == "?"s) {
                auto c = get_color(index);
                if (c == nullptr && index_fallback != -1)
                        c = get_color(index_fallback);
                g_assert_nonnull(c);

                reply(seq, VTE_REPLY_OSC, {}, "%d;rgb:%04x/%04x/%04x",
                      osc, c->red, c->green, c->blue);
        } else {
                vte::color::rgb color;
                if (color.parse(str.data())) {
                        set_color(index, VTE_COLOR_SOURCE_ESCAPE, color);

                        /* emit the refresh as the palette has changed and previous
                         * renders need to be updated. */
                        emit_refresh_window();
                }
        }
}

void
VteTerminalPrivate::reset_color(vte::parser::Sequence const& seq,
                                vte::parser::StringTokeniser::const_iterator& token,
                                vte::parser::StringTokeniser::const_iterator const& endtoken) noexcept
{
        /* Empty param? Reset all */
        if (token == endtoken ||
            token.size_remaining() == 0) {
                for (unsigned int idx = 0; idx < VTE_DEFAULT_FG; idx++)
                        reset_color(idx, VTE_COLOR_SOURCE_ESCAPE);

                /* emit the refresh as the palette has changed and previous
                 * renders need to be updated. */
                emit_refresh_window();
                return;
        }

        bool any_changed = false;

        while (token != endtoken) {
                int value;
                if (!token.number(value))
                        continue;

                if (0 <= value && value < VTE_DEFAULT_FG) {
                        reset_color(value, VTE_COLOR_SOURCE_ESCAPE);
                        any_changed = true;
                }

                ++token;
        }

        /* emit the refresh as the palette has changed and previous
         * renders need to be updated. */
        if (any_changed)
                emit_refresh_window();
}

void
VteTerminalPrivate::set_current_directory_uri(vte::parser::Sequence const& seq,
                                              vte::parser::StringTokeniser::const_iterator& token,
                                              vte::parser::StringTokeniser::const_iterator const& endtoken) noexcept
{
        std::string uri;
        if (token != endtoken && token.size_remaining() > 0) {
                uri = token.string_remaining();

                auto filename = g_filename_from_uri(uri.data(), nullptr, nullptr);
                if (filename != nullptr) {
                        g_free(filename);
                } else {
                        /* invalid URI */
                        uri.clear();
                }
        }

        m_current_directory_uri_pending.swap(uri);
        m_current_directory_uri_changed = true;
}

void
VteTerminalPrivate::set_current_file_uri(vte::parser::Sequence const& seq,
                                         vte::parser::StringTokeniser::const_iterator& token,
                                         vte::parser::StringTokeniser::const_iterator const& endtoken) noexcept

{
        std::string uri;
        if (token != endtoken && token.size_remaining() > 0) {
                uri = token.string_remaining();

                auto filename = g_filename_from_uri(uri.data(), nullptr, nullptr);
                if (filename != nullptr) {
                        g_free(filename);
                } else {
                        /* invalid URI */
                        uri.clear();
                }
        }

        m_current_file_uri_pending.swap(uri);
        m_current_file_uri_changed = true;
}

void
VteTerminalPrivate::set_current_hyperlink(vte::parser::Sequence const& seq,
                                          vte::parser::StringTokeniser::const_iterator& token,
                                          vte::parser::StringTokeniser::const_iterator const& endtoken) noexcept
{
        if (token == endtoken)
                return; // FIXMEchpe or should we treat this as a reset?

        /* Handle OSC 8 hyperlinks.
         * See bug 779734 and https://gist.github.com/egmontkob/eb114294efbcd5adb1944c9f3cb5feda
         */

        if (!m_allow_hyperlink)
                return;

        /* The hyperlink, as we carry around and store in the streams, is "id;uri" */
        std::string hyperlink;

        /* First, find the ID */
        vte::parser::StringTokeniser subtokeniser{*token, ':'};
        for (auto subtoken : subtokeniser) {
                auto const len = subtoken.size();
                if (len < 3)
                        continue;

                if (subtoken[0] != 'i' || subtoken[1] != 'd' || subtoken[2] != '=')
                        continue;

                if (len > 3 + VTE_HYPERLINK_ID_LENGTH_MAX) {
                        _vte_debug_print (VTE_DEBUG_HYPERLINK, "Overlong \"id\" ignored: \"%s\"\n",
                                          subtoken.data());
                        break;
                }

                hyperlink = subtoken.substr(3);
                break;
        }

        if (hyperlink.size() == 0) {
                /* Automatically generate a unique ID string. The colon makes sure
                 * it cannot conflict with an explicitly specified one.
                 */
                char idbuf[24];
                auto len = g_snprintf(idbuf, sizeof(idbuf), ":%ld", m_hyperlink_auto_id++);
                hyperlink.append(idbuf, len);
                _vte_debug_print (VTE_DEBUG_HYPERLINK, "Autogenerated id=\"%s\"\n", hyperlink.data());
        }

        /* Now get the URI */
        if (++token == endtoken)
                return; // FIXMEchpe or should we treat this the same as 0-length URI ?

        hyperlink.push_back(';');
        guint idx;
        auto const len = token.size_remaining();
        if (len > 0 && len <= VTE_HYPERLINK_URI_LENGTH_MAX) {
                token.append_remaining(hyperlink);

                _vte_debug_print (VTE_DEBUG_HYPERLINK, "OSC 8: id;uri=\"%s\"\n", hyperlink.data());

                idx = _vte_ring_get_hyperlink_idx(m_screen->row_data, hyperlink.data());
        } else {
                if (G_UNLIKELY(len > VTE_HYPERLINK_URI_LENGTH_MAX))
                        _vte_debug_print (VTE_DEBUG_HYPERLINK, "Overlong URI ignored (len %" G_GSIZE_FORMAT ")\n", len);

                /* idx = 0; also remove the previous current_idx so that it can be GC'd now. */
                idx = _vte_ring_get_hyperlink_idx(m_screen->row_data, nullptr);
        }

        m_defaults.attr.hyperlink_idx = idx;
}

/*
 * Command Handlers
 * This is the unofficial documentation of all the VTE_CMD_* definitions.
 * Each handled command has a separate function with an extensive comment on
 * the semantics of the command.
 * Note that many semantics are unknown and need to be verified. This is mostly
 * about error-handling, though. Applications rarely rely on those features.
 */

void
VteTerminalPrivate::NONE(vte::parser::Sequence const& seq)
{
}

void
VteTerminalPrivate::GRAPHIC(vte::parser::Sequence const& seq)
{
#if 0
        struct vte_char ch = VTE_CHAR_NULL;

        if (screen->state.cursor_x + 1 == screen->page->width
            && screen->flags & VTE_FLAG_PENDING_WRAP
            && screen->state.auto_wrap) {
                screen_cursor_down(screen, 1, true);
                screen_cursor_set(screen, 0, screen->state.cursor_y);
        }

        screen_cursor_clear_wrap(screen);

        ch = vte_char_merge(ch, screen_map(screen, seq->terminator));
        vte_page_write(screen->page,
                          screen->state.cursor_x,
                          screen->state.cursor_y,
                          ch,
                          1,
                          &screen->state.attr,
                          screen->age,
                          false);

        if (screen->state.cursor_x + 1 == screen->page->width)
                screen->flags |= VTE_FLAG_PENDING_WRAP;
        else
                screen_cursor_right(screen, 1);

        return 0;
#endif

        insert_char(seq.terminator(), false, false);
}


void
VteTerminalPrivate::ACS(vte::parser::Sequence const& seq)
{
        /* ACS - announce-code-structure
         *
         * The final byte of the sequence identifies the facility number
         * from 1 to 62 starting with 4/01.
         *
         * References: ECMA-35 § 15.2
         */

        /* Since we don't implement ISO-2022 anymore, we can mostly ignore this */

        switch (seq.terminator() - 0x40) {
        case 6:
                /*
                 * This causes the terminal to start sending C1 controls as 7bit
                 * sequences instead of 8bit C1 controls.
                 * This is ignored if the terminal is below level-2 emulation mode
                 * (VT100 and below), the terminal already sends 7bit controls then.
                 */
#if 0
                if (screen->conformance_level > VTE_CONFORMANCE_LEVEL_VT100)
                        screen->flags |= VTE_FLAG_7BIT_MODE;
#endif
                break;

        case 7:
                /*
                 * This causes the terminal to start sending C1 controls as 8bit C1
                 * control instead of 7bit sequences.
                 * This is ignored if the terminal is below level-2 emulation mode
                 * (VT100 and below). The terminal always sends 7bit controls in those
                 * modes.
                 */
#if 0
                if (screen->conformance_level > VTE_CONFORMANCE_LEVEL_VT100)
                        screen->flags &= ~VTE_FLAG_7BIT_MODE;
#endif
                break;

        case 12:
                /* Use Level 1 of ECMA-43
                 *
                 * Probably not worth implementing.
                 */
                break;
        case 13:
                /* Use Level 2 of ECMA-43
                 *
                 * Probably not worth implementing.
                 */
                break;
        case 14:
                /* Use Level 3 of ECMA-43
                 *
                 * Probably not worth implementing.
                 */
                break;
        }
}

void
VteTerminalPrivate::BEL(vte::parser::Sequence const& seq)
{
        /*
         * BEL - sound bell tone
         * This command should trigger an acoustic bell.
         *
         * References: ECMA-48 § 8.3.3
         */

        m_bell_pending = true;
}

void
VteTerminalPrivate::BS(vte::parser::Sequence const& seq)
{
        /*
         * BS - backspace
         * Move cursor one cell to the left. If already at the left margin,
         * nothing happens.
         *
         * References: ECMA-48 § 8.3.5
         */

#if 0
        screen_cursor_clear_wrap(screen);
        screen_cursor_left(screen, 1);
#endif

        ensure_cursor_is_onscreen();

        if (m_screen->cursor.col > 0) {
		/* There's room to move left, so do so. */
                m_screen->cursor.col--;
	}
}

void
VteTerminalPrivate::CBT(vte::parser::Sequence const& seq)
{
        /*
         * CBT - cursor-backward-tabulation
         * Move the cursor @args[0] tabs backwards (to the left). The
         * current cursor cell, in case it's a tab, is not counted.
         * Furthermore, the cursor cannot be moved beyond position 0 and
         * it will stop there.
         *
         * Defaults:
         *   args[0]: 1
         *
         * References: ECMA-48 § 8.3.7
         */
#if 0
        screen_cursor_clear_wrap(screen);
#endif

        move_cursor_tab_backward(seq.collect1(0, 1));
}

void
VteTerminalPrivate::CHA(vte::parser::Sequence const& seq)
{
        /*
         * CHA - cursor-horizontal-absolute
         * Move the cursor to position @args[0] in the current line. The
         * cursor cannot be moved beyond the rightmost cell and will stop
         * there.
         *
         * Note: This does the same as HPA
         *
         * Defaults:
         *   args[0]: 1
         *
         * References: ECMA-48 § 8.3.9
         */

#if 0
        unsigned int pos = 1;

        if (seq->args[0] > 0)
                pos = seq->args[0];

        screen_cursor_clear_wrap(screen);
        screen_cursor_set(screen, pos - 1, screen->state.cursor_y);
#endif

        auto value = seq.collect1(0, 1, 1, m_column_count);
        set_cursor_column1(value);
}

void
VteTerminalPrivate::CHT(vte::parser::Sequence const& seq)
{
        /*
         * CHT - cursor-horizontal-forward-tabulation
         * Move the cursor @args[0] tabs forward (to the right). The
         * current cursor cell, in case it's a tab, is not counted.
         * Furthermore, the cursor cannot be moved beyond the rightmost cell
         * and will stop there.
         *
         * Defaults:
         *   args[0]: 1
         *
         * References: ECMA-48 § 8.3.10
         */
#if 0
        screen_cursor_clear_wrap(screen);
#endif

        move_cursor_tab_forward(seq.collect1(0, 1));
}

void
VteTerminalPrivate::CMD(vte::parser::Sequence const& seq)
{
        /*
         * CMD - coding method delimiter
         *
         * References: ECMA-35 §15.3
         */
}

void
VteTerminalPrivate::CNL(vte::parser::Sequence const& seq)
{
        /*
         * CNL - cursor-next-line
         * Move the cursor @args[0] lines down.
         *
         * TODO: Does this stop at the bottom or cause a scroll-up?
         *
         * Defaults:
         *   args[0]: 1
         *
         * References: ECMA-48 §8.3.12
         */
#if 0
        unsigned int num = 1;

        if (seq->args[0] > 0)
                num = seq->args[0];

        screen_cursor_clear_wrap(screen);
        screen_cursor_down(screen, num, false);
#endif

        set_cursor_column1(1);

        auto value = seq.collect1(0, 1);
        move_cursor_down(value);
}

void
VteTerminalPrivate::CPL(vte::parser::Sequence const& seq)
{
        /*
         * CPL - cursor-preceding-line
         * Move the cursor @args[0] lines up, without scrolling.
         *
         * Defaults:
         *   args[0]: 1
         *
         * References: ECMA-48 § 8.3.13
         */
#if 0
        unsigned int num = 1;

        if (seq->args[0] > 0)
                num = seq->args[0];

        screen_cursor_clear_wrap(screen);
        screen_cursor_up(screen, num, false);
#endif

        set_cursor_column(0);

        auto const value = seq.collect1(0, 1);
        move_cursor_up(value);
}

void
VteTerminalPrivate::CR(vte::parser::Sequence const& seq)
{
        /*
         * CR - carriage-return
         * Move the cursor to the left margin on the current line.
         *
         * References: ECMA-48 § 8.3.15
         */
#if 0
        screen_cursor_clear_wrap(screen);
        screen_cursor_set(screen, 0, screen->state.cursor_y);
#endif

        set_cursor_column(0);
}

void
VteTerminalPrivate::CUB(vte::parser::Sequence const& seq)
{
        /*
         * CUB - cursor-backward
         * Move the cursor @args[0] positions to the left. The cursor stops
         * at the left-most position.
         *
         * Defaults:
         *   args[0]: 1
         *
         * References: ECMA-48 § 8.3.18
         */
#if 0
        unsigned int num = 1;

        if (seq->args[0] > 0)
                num = seq->args[0];

        screen_cursor_clear_wrap(screen);
        screen_cursor_left(screen, num);
#endif

        auto value = seq.collect1(0, 1);
        move_cursor_backward(value);
}

void
VteTerminalPrivate::CUD(vte::parser::Sequence const& seq)
{
        /*
         * CUD - cursor-down
         * Move the cursor @args[0] positions down. The cursor stops at the
         * bottom margin. If it was already moved further, it stops at the
         * bottom line.
         *
         * Defaults:
         *   args[0]: 1
         *
         * References: ECMA-48 § 8.3.19
         */
#if 0
        unsigned int num = 1;

        if (seq->args[0] > 0)
                num = seq->args[0];

        screen_cursor_clear_wrap(screen);
        screen_cursor_down(screen, num, false);
#endif

        auto value = seq.collect1(0, 1);
        move_cursor_down(value);
}

void
VteTerminalPrivate::CUF(vte::parser::Sequence const& seq)
{
        /*
         * CUF -cursor-forward
         * Move the cursor @args[0] positions to the right. The cursor stops
         * at the right-most position.
         *
         * Defaults:
         *   args[0]: 1
         *
         * References: ECMA-48 § 8.3.20
         */
#if 0
        unsigned int num = 1;

        if (seq->args[0] > 0)
                num = seq->args[0];

        screen_cursor_clear_wrap(screen);
        screen_cursor_right(screen, num);
#endif

        auto value = seq.collect1(0, 1);
        move_cursor_forward(value);
}

void
VteTerminalPrivate::CUP(vte::parser::Sequence const& seq)
{
        /*
         * CUP - cursor-position
         * Moves the cursor to position @args[1] x @args[0]. If either is 0, it
         * is treated as 1. The positions are subject to the origin-mode and
         * clamped to the addressable with/height.
         *
         * Defaults:
         *   args[0]: 1
         *   args[1]: 1
         *
         * References: ECMA-48 § 8.3.21
         */
#if 0
        unsigned int x = 1, y = 1;

        if (seq->args[0] > 0)
                y = seq->args[0];
        if (seq->args[1] > 0)
                x = seq->args[1];

        screen_cursor_clear_wrap(screen);
        screen_cursor_set_rel(screen, x - 1, y - 1);
#endif

        /* The first is the row, the second is the column. */
        auto rowvalue = seq.collect1(0, 1, 1, m_row_count);
        auto colvalue = seq.collect1(seq.next(0), 1, 1, m_column_count);
        set_cursor_coords1(rowvalue, colvalue);
}

void
VteTerminalPrivate::CUU(vte::parser::Sequence const& seq)
{
        /*
         * CUU - cursor-up
         * Move the cursor @args[0] positions up. The cursor stops at the
         * top margin. If it was already moved further, it stops at the
         * top line.
         *
         * Defaults:
         *   args[0]: 1
         *
         * References: ECMA-48 § 8.3.22
         */
#if 0
        unsigned int num = 1;

        if (seq->args[0] > 0)
                num = seq->args[0];

        screen_cursor_clear_wrap(screen);
        screen_cursor_up(screen, num, false);
#endif

        auto const value = seq.collect1(0, 1);
        move_cursor_up(value);
}

void
VteTerminalPrivate::CnD(vte::parser::Sequence const& seq)
{
        /*
         * CnD - Cn-designate
         *
         * Designate a set of control functions.
         *
         * References: ECMA-35 § 14.2
         *             ISO 2375 IR
         */

        /* Since we don't implement ISO-2022 anymore, we can ignore this */
}

void
VteTerminalPrivate::DA1(vte::parser::Sequence const& seq)
{
        /*
         * DA1 - primary-device-attributes
         * The primary DA asks for basic terminal features. We simply return
         * a hard-coded list of features we implement.
         * Note that the primary DA asks for supported features, not currently
         * enabled features.
         *
         * The terminal's answer is:
         *   ^[ ? 65 ; ARGS c
         * The first argument, 65, is fixed and denotes a VT520, the last
         * DEC-term that extended this number.
         * All following arguments denote supported features. Note
         * that at most 15 features can be sent (max CSI args). It is safe to
         * send more, but clients might not be able to parse them. This is a
         * client's problem and we shouldn't care. There is no other way to
         * send those feature lists, so we have to extend them beyond 15 in
         * those cases.
         *
         * Known modes:
         *    1: 132 column mode
         *       The 132 column mode is supported by the terminal.
         *    2: printer port
         *       A priner-port is supported and can be addressed via
         *       control-codes.
         *    3: ReGIS graphics
         *       Support for ReGIS graphics is available. The ReGIS routines
         *       provide the "remote graphics instruction set" and allow basic
         *       vector-rendering.
         *    4: Sixel
         *       Support of Sixel graphics is available. This provides access
         *       to the sixel bitmap routines.
         *    6: selective erase
         *       The terminal supports DECSCA and related selective-erase
         *       functions. This allows to protect specific cells from being
         *       erased, if specified.
         *    7: soft character set (DRCS)
         *       TODO: ?
         *    8: user-defined keys (UDKs)
         *       TODO: ?
         *    9: national-replacement character sets (NRCS)
         *       National-replacement character-sets are available.
         *   12: Serbo-Croatian (SCS)
         *       TODO: ?
         *   15: technical character set
         *       The DEC technical-character-set is available.
         *   18: windowing capability
         *       TODO: ?
         *   21: horizontal scrolling
         *       TODO: ?
         *   22: ANSI color
         *       TODO: ?
         *   23: Greek
         *       TODO: ?
         *   24: Turkish
         *       TODO: ?
         *   29: DECterm text locator
         *       TODO: ?
         *   42: ISO Latin-2 character set
         *       TODO: ?
         *   44: PCTerm
         *       TODO: ?
         *   45: soft keymap
         *       TODO: ?
         *   46: ASCII emulation
         *       TODO: ?
         *
         * Defaults:
         *   args[0]: 0
         *
         * References: ECMA-48 § 8.3.24
         *             VT525
         */

        if (seq.collect1(0, 0) != 0)
                return;

        reply(seq, VTE_REPLY_DECDA1R, {62});
}

void
VteTerminalPrivate::DA2(vte::parser::Sequence const& seq)
{
        /*
         * DA2 - secondary-device-attributes
         * The secondary DA asks for the terminal-ID, firmware versions and
         * other non-primary attributes. All these values are
         * informational-only and should not be used by the host to detect
         * terminal features.
         *
         * The terminal's response is:
         *   ^[ > 65 ; FIRMWARE ; KEYBOARD c
         * whereas 65 is fixed for VT525 terminals, the last terminal-line that
         * increased this number. FIRMWARE is the firmware
         * version encoded as major/minor (20 == 2.0) and KEYBOARD is 0 for STD
         * keyboard and 1 for PC keyboards.
         *
         * We replace the firmware-version with the VTE version so clients
         * can decode it again.
         *
         * References: VT525
         */

        /* Param != 0 means this is a reply, not a request */
        if (seq.collect1(0, 0) != 0)
                return;

        int const version = (VTE_MAJOR_VERSION * 100 + VTE_MINOR_VERSION) * 100 + VTE_MICRO_VERSION;
        reply(seq, VTE_REPLY_DECDA2R, {65, version, 1});
}

void
VteTerminalPrivate::DA3(vte::parser::Sequence const& seq)
{
        /*
         * DA3 - tertiary-device-attributes
         * The tertiary DA is used to query the terminal-ID.
         *
         * The terminal's response is:
         *   ^P ! | XX AA BB CC ^\
         * whereas all four parameters are hexadecimal-encoded pairs. XX
         * denotes the manufacturing site, AA BB CC is the terminal's ID.
         *
         * We always reply with '~VTE' encoded in hex.
         */

        if (seq.collect1(0, 0) != 0)
                return;

        reply(seq, VTE_REPLY_DECRPTUI, {});
}

void
VteTerminalPrivate::DC1(vte::parser::Sequence const& seq)
{
        /*
         * DC1 - device-control-1 or XON
         * This clears any previous XOFF and resumes terminal-transmission.
         */

        /* we do not support XON */
}

void
VteTerminalPrivate::DC3(vte::parser::Sequence const& seq)
{
        /*
         * DC3 - device-control-3 or XOFF
         * Stops terminal transmission. No further characters are sent until
         * an XON is received.
         */

        /* we do not support XOFF */
}

void
VteTerminalPrivate::DCH(vte::parser::Sequence const& seq)
{
        /*
         * DCH - delete-character
         * This deletes @argv[0] characters at the current cursor position.
         *
         * Defaults:
         *   args[0]: 1
         *
         * References: ECMA-48 § 8.3.26
         */
#if 0
        unsigned int num = 1;

        if (seq->args[0] > 0)
                num = seq->args[0];

        screen_cursor_clear_wrap(screen);
        vte_page_delete_cells(screen->page,
                                 screen->state.cursor_x,
                                 screen->state.cursor_y,
                                 num,
                                 &screen->state.attr,
                                 screen->age);
#endif

        auto const value = seq.collect1(0, 1, 1, int(m_column_count - m_screen->cursor.col));

        // FIXMEchpe pass count to delete_character() and simplify
        // to only cleanup fragments once
        for (auto i = 0; i < value; i++)
                delete_character();
}

void
VteTerminalPrivate::DECALN(vte::parser::Sequence const& seq)
{
        /*
         * DECALN - screen-alignment-pattern
         *
         * Probably not worth implementing.
         *
         * References: VT525
         */

        // FIXMEchpe why do we implement this?
	for (auto row = m_screen->insert_delta;
	     row < m_screen->insert_delta + m_row_count;
	     row++) {
		/* Find this row. */
                while (_vte_ring_next(m_screen->row_data) <= row)
                        ring_append(false);
                adjust_adjustments();
                auto rowdata = _vte_ring_index_writable (m_screen->row_data, row);
		g_assert(rowdata != NULL);
		/* Clear this row. */
		_vte_row_data_shrink (rowdata, 0);

                emit_text_deleted();
		/* Fill this row. */
                VteCell cell;
		cell.c = 'E';
		cell.attr = basic_cell.attr;
		cell.attr.set_columns(1);
                _vte_row_data_fill(rowdata, &cell, m_column_count);
                emit_text_inserted();
	}
        invalidate_all();

	/* We modified the display, so make a note of it for completeness. */
        m_text_modified_flag = TRUE;
}

void
VteTerminalPrivate::DECANM(vte::parser::Sequence const& seq)
{
        /*
         * DECANM - ansi-mode
         * Set the terminal into VT52 compatibility mode. Control sequences
         * overlap with regular sequences so we have to detect them early before
         * dispatching them.
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECAUPSS(vte::parser::Sequence const& seq)
{
        /*
         * DECAUPSS - assign user preferred supplemental sets
         *
         * References: VT525
         */
}

void
VteTerminalPrivate::DECBI(vte::parser::Sequence const& seq)
{
        /*
         * DECBI - back-index
         * This control function moves the cursor backward one column. If the
         * cursor is at the left margin, then all screen data within the margin
         * moves one column to the right. The column that shifted past the right
         * margin is lost.
         * DECBI adds a new column at the left margin with no visual attributes.
         * DECBI does not affect the margins. If the cursor is beyond the
         * left-margin at the left border, then the terminal ignores DECBI.
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECCARA(vte::parser::Sequence const& seq)
{
        /*
         * DECCARA - change-attributes-in-rectangular-area
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECCKD(vte::parser::Sequence const& seq)
{
        /*
         * DECCKD - copy key default
         *
         * References: VT525
         */
}

void
VteTerminalPrivate::DECCRA(vte::parser::Sequence const& seq)
{
        /*
         * DECCRA - copy-rectangular-area
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECDC(vte::parser::Sequence const& seq)
{
        /*
         * DECDC - delete-column
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECDHL_BH(vte::parser::Sequence const& seq)
{
        /*
         * DECDHL_BH - double-width-double-height-line: bottom half
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECDHL_TH(vte::parser::Sequence const& seq)
{
        /*
         * DECDHL_TH - double-width-double-height-line: top half
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECDLD(vte::parser::Sequence const& seq)
{
        /*
         * DECDLD - dynamically redefinable character sets extension
         *
         * References: VT525
         */
}

void
VteTerminalPrivate::DECDMAC(vte::parser::Sequence const& seq)
{
        /*
         * DECDMAC - define-macro
         *
         * References: VT525
         */
}

void
VteTerminalPrivate::DECDWL(vte::parser::Sequence const& seq)
{
        /*
         * DECDWL - double-width-single-height-line
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECEFR(vte::parser::Sequence const& seq)
{
        /*
         * DECEFR - enable-filter-rectangle
         * Defines the coordinates of a filter rectangle (top, left, bottom,
         * right as @args[0] to @args[3]) and activates it.
         * Anytime the locator is detected outside of the filter rectangle, an
         * outside rectangle event is generated and the rectangle is disabled.
         * Filter rectangles are always treated as "one-shot" events. Any
         * parameters that are omitted default to the current locator position.
         * If all parameters are omitted, any locator motion will be reported.
         * DECELR always cancels any prevous rectangle definition.
         *
         * The locator is usually associated with the mouse-cursor, but based
         * on cells instead of pixels. See DECELR how to initialize and enable
         * it. DECELR can also enable pixel-mode instead of cell-mode.
         *
         * TODO: implement
         */
}

void
VteTerminalPrivate::DECELF(vte::parser::Sequence const& seq)
{
        /*
         * DECELF - enable-local-functions
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECELR(vte::parser::Sequence const& seq)
{
        /*
         * DECELR - enable-locator-reporting
         * This changes the locator-reporting mode. @args[0] specifies the mode
         * to set, 0 disables locator-reporting, 1 enables it continuously, 2
         * enables it for a single report. @args[1] specifies the
         * precision-mode. 0 and 2 set the reporting to cell-precision, 1 sets
         * pixel-precision.
         *
         * Defaults:
         *   args[0]: 0
         *   args[1]: 0
         *
         * TODO: implement
         */
}

void
VteTerminalPrivate::DECERA(vte::parser::Sequence const& seq)
{
        /*
         * DECERA - erase-rectangular-area
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECFI(vte::parser::Sequence const& seq)
{
        /*
         * DECFI - forward-index
         * This control function moves the cursor forward one column. If the
         * cursor is at the right margin, then all screen data within the
         * margins moves one column to the left. The column shifted past the
         * left margin is lost.
         * DECFI adds a new column at the right margin, with no visual
         * attributes. DECFI does not affect margins. If the cursor is beyond
         * the right margin at the border of the page when the terminal
         * receives DECFI, then the terminal ignores DECFI.
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECFRA(vte::parser::Sequence const& seq)
{
        /*
         * DECFRA - fill-rectangular-area
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECIC(vte::parser::Sequence const& seq)
{
        /*
         * DECIC - insert-column
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECINVM(vte::parser::Sequence const& seq)
{
        /*
         * DECINVM - invoke-macro
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECKBD(vte::parser::Sequence const& seq)
{
        /*
         * DECKBD - keyboard-language-selection
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECKPAM(vte::parser::Sequence const& seq)
{
        /*
         * DECKPAM - keypad-application-mode
         * Enables the keypad-application mode. If enabled, the keypad sends
         * special characters instead of the printed characters. This way,
         * applications can detect whether a numeric key was pressed on the
         * top-row or on the keypad.
         * Default is keypad-numeric-mode.
         */

        set_mode_private(vte::terminal::modes::Private::eDEC_APPLICATION_KEYPAD, true);
}

void
VteTerminalPrivate::DECKPNM(vte::parser::Sequence const& seq)
{
        /*
         * DECKPNM - keypad-numeric-mode
         * This disables the keypad-application-mode (DECKPAM) and returns to
         * the keypad-numeric-mode. Keypresses on the keypad generate the same
         * sequences as corresponding keypresses on the main keyboard.
         * Default is keypad-numeric-mode.
         */
        set_mode_private(vte::terminal::modes::Private::eDEC_APPLICATION_KEYPAD, false);
}

void
VteTerminalPrivate::DECLANS(vte::parser::Sequence const& seq)
{
        /*
         * DECLANS - load answerback message
         *
         * Will not implement this because of security policy.
         *
         * References: VT525
         */
}

void
VteTerminalPrivate::DECLBAN(vte::parser::Sequence const& seq)
{
        /*
         * DECLBAN - load banner message
         *
         * References: VT525
         */
}

void
VteTerminalPrivate::DECLBD(vte::parser::Sequence const& seq)
{
        /*
         * DECLBD - locator button define
         *
         * References: VT330
         */
}

void
VteTerminalPrivate::DECLFKC(vte::parser::Sequence const& seq)
{
        /*
         * DECLFKC - local-function-key-control
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECLL(vte::parser::Sequence const& seq)
{
        /*
         * DECLL - load-leds
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECLTOD(vte::parser::Sequence const& seq)
{
        /*
         * DECLTOD - load-time-of-day
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECPAK(vte::parser::Sequence const& seq)
{
        /*
         * DECPAK - program alphanumeric key
         *
         * References: VT525
         */
}

void
VteTerminalPrivate::DECPCTERM(vte::parser::Sequence const& seq)
{
        /*
         * DECPCTERM - pcterm-mode
         * This enters/exits the PCTerm mode. Default mode is VT-mode. It can
         * also select parameters for scancode/keycode mappings in SCO mode.
         *
         * Definitely not worth implementing. Lets kill PCTerm/SCO modes!
         */
}

void
VteTerminalPrivate::DECPCTERM_OR_XTERM_RPM(vte::parser::Sequence const& seq)
{
        /*
         * There's a conflict between DECPCTERM and XTERM-RPM.
         * XTERM-RPM takes a single argument, DECPCTERM takes 2.
         * Note that since both admit default values (which may be
         * omitted at the end of the sequence), this only an approximation.
         */
        if (seq.size_final() <= 1)
                XTERM_RPM(seq);
        else
                DECPCTERM(seq);
}

void
VteTerminalPrivate::DECPFK(vte::parser::Sequence const& seq)
{
        /*
         * DECPFK - program function key
         *
         * References: VT525
         */
}

void
VteTerminalPrivate::DECPKA(vte::parser::Sequence const& seq)
{
        /*
         * DECPKA - program-key-action
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECPKFMR(vte::parser::Sequence const& seq)
{
        /*
         * DECPKFMR - program-key-free-memory-report
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECRARA(vte::parser::Sequence const& seq)
{
        /*
         * DECRARA - reverse-attributes-in-rectangular-area
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECRC(vte::parser::Sequence const& seq)
{
        /*
         * DECRC - restore-cursor
         * Restores the terminal to the state saved by the save cursor (DECSC)
         * function. If there was not a previous DECSC, then this does:
         *   * Home the cursor
         *   * Resets DECOM
         *   * Resets the SGR attributes
         *   * Designates ASCII (IR #6) to GL, and DEC Supplemental Graphics to GR
         *
         * Note that the status line has its own DECSC buffer.
         *
         * References: VT525
         */
#if 0
        screen_restore_state(screen, &screen->saved);
#endif

        restore_cursor();
}

void
VteTerminalPrivate::DECREGIS(vte::parser::Sequence const& seq)
{
        /*
         * DECREGIS - ReGIS graphics
         *
         * References: VT330
         */
}

void
VteTerminalPrivate::DECREQTPARM(vte::parser::Sequence const& seq)
{
        /*
         * DECREQTPARM - request-terminal-parameters
         * The sequence DECREPTPARM is sent by the terminal controller to notify
         * the host of the status of selected terminal parameters. The status
         * sequence may be sent when requested by the host or at the terminal's
         * discretion. DECREPTPARM is sent upon receipt of a DECREQTPARM.
         *
         * If @args[0] is 0, this marks a request and the terminal is allowed
         * to send DECREPTPARM messages without request. If it is 1, the same
         * applies but the terminal should no longer send DECREPTPARM
         * unrequested.
         * 2 and 3 mark a report, but 3 is only used if the terminal answers as
         * an explicit request with @args[0] == 1.
         *
         * The other arguments are ignored in requests, but have the following
         * meaning in responses:
         *   args[1]: 1=no-parity-set 4=parity-set-and-odd 5=parity-set-and-even
         *   args[2]: 1=8bits-per-char 2=7bits-per-char
         *   args[3]: transmission-speed
         *   args[4]: receive-speed
         *   args[5]: 1=bit-rate-multiplier-is-16
         *   args[6]: This value communicates the four switch values in block 5
         *            of SETUP B, which are only visible to the user when an STP
         *            option is installed. These bits may be assigned for an STP
         *            device. The four bits are a decimal-encoded binary number.
         *            Value between 0-15.
         *
         * The transmission/receive speeds have mappings for number => bits/s
         * which are quite weird. Examples are: 96->3600, 112->9600, 120->19200
         *
         * Defaults:
         *   args[0]: 0
         *
         * References: VT100
         */

        switch (seq.collect1(0)) {
        case -1:
        case 0:
                #if 0
                screen->flags &= ~VTE_FLAG_INHIBIT_TPARM;
                #endif
                reply(seq, VTE_REPLY_DECREPTPARM,
                      {2, 1, 1, 120, 120, 1, 0});
                break;
        case 1:
                #if 0
                screen->flags |= VTE_FLAG_INHIBIT_TPARM;
                #endif
                reply(seq, VTE_REPLY_DECREPTPARM,
                      {3, 1, 1, 120, 120, 1, 0});
                break;
        case 2:
        case 3:
                /* This is a report, not a request */
        default:
                break;
        }
}

void
VteTerminalPrivate::DECRPKT(vte::parser::Sequence const& seq)
{
        /*
         * DECRPKT - report-key-type
         * Response to DECRQKT, we can safely ignore it as we're the one sending
         * it to the host.
         */
}

void
VteTerminalPrivate::DECRQCRA(vte::parser::Sequence const& seq)
{
        /*
         * DECRQCRA - request checksum of rectangular area
         * Computes a simple checksum of the characters in the rectangular
         * area. args[0] is an identifier, which the response must use.
         * args[1] is the page number; if it's 0 or default then the
         * checksum is computed over all pages; if it's greater than the
         * number of pages, then the checksum is computed only over the
         * last page. args[2]..args[5] describe the area to compute the
         * checksum from, denoting the top, left, bottom, right, resp
         * (1-based). It's required that top ≤ bottom, and left ≤ right.
         * These coordinates are interpreted according to origin mode.
         *
         * NOTE: Since this effectively allows to read the screen
         * (by using a 1x1 rectangle on each cell), we normally only
         * send a dummy reply, and only reply with the actual checksum
         * when in test mode.
         *
         * Defaults:
         *   args[0]: no default
         *   args[1]: 0
         *   args[2]: 1
         *   args[3]: no default (?)
         *   args[4]: height of current page
         *   args[5]: width of current page
         *
         * Reply: DECCKSR
         *   @args[0]: the identifier from the request
         *   DATA: the checksum as a 4-digit hex number
         *
         * References: VT525
         *             XTERM
         */

        unsigned int idx = 0;
        int id = seq.collect1(idx);

#ifndef VTE_DEBUG
        /* Send a dummy reply */
        return reply(seq, VTE_REPLY_DECCKSR, {id}, "0000");
#else

        /* Not in test mode? Send a dummy reply */
        if (!g_test_mode) {
                return reply(seq, VTE_REPLY_DECCKSR, {id}, "0000");
        }

        idx = seq.next(idx);

        /* We only support 1 'page', so ignore args[1] */
        idx = seq.next(idx);

        int top = seq.collect1(idx, 1, 1, m_row_count);
        idx = seq.next(idx);
        int left = seq.collect1(idx, 1, 1, m_column_count); /* use 1 as default here */
        idx = seq.next(idx);
        int bottom = seq.collect1(idx, m_row_count, 1, m_row_count);
        idx = seq.next(idx);
        int right = seq.collect1(idx, m_column_count, 1, m_column_count);

        if (m_modes_private.DEC_ORIGIN() &&
            m_scrolling_restricted) {
                top += m_scrolling_region.start;

                bottom += m_scrolling_region.start;
                bottom = std::min(bottom, m_scrolling_region.end);

        }

        unsigned int checksum;
        if (bottom < top || right < left)
                checksum = 0; /* empty area */
        else
                checksum = checksum_area(top -1 + m_screen->insert_delta,
                                         left - 1,
                                         bottom - 1 + m_screen->insert_delta,
                                         right - 1);

        reply(seq, VTE_REPLY_DECCKSR, {id}, "%04X", checksum);
#endif /* VTE_DEBUG */
}

void
VteTerminalPrivate::DECRQDE(vte::parser::Sequence const& seq)
{
        /*
         * DECRQDE - request-display-extent
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECRQKT(vte::parser::Sequence const& seq)
{
        /*
         * DECRQKT - request-key-type
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECRQLP(vte::parser::Sequence const& seq)
{
        /*
         * DECRQLP - request-locator-position
         * See DECELR for locator-information.
         *
         * TODO: document and implement
         */
}

void
VteTerminalPrivate::DECRQM_ECMA(vte::parser::Sequence const& seq)
{
        /*
         * DECRQM_ECMA - request-mode-ecma
         * The host sends this control function to find out if a particular mode
         * is set or reset. The terminal responds with a report mode function.
         * @args[0] contains the mode to query.
         *
         * Response is DECRPM with the first argument set to the mode that was
         * queried, second argument is 0 if mode is invalid, 1 if mode is set,
         * 2 if mode is not set (reset), 3 if mode is permanently set and 4 if
         * mode is permanently not set (reset):
         *   ECMA: ^[ MODE ; VALUE $ y
         *   DEC:  ^[ ? MODE ; VALUE $ y
         *
         * References: VT525
         */

        auto const param = seq.collect1(0);
        auto const mode = m_modes_ecma.mode_from_param(param);

        int value;
        switch (mode) {
        case vte::terminal::modes::ECMA::eUNKNOWN:      value = 0; break;
        case vte::terminal::modes::ECMA::eALWAYS_SET:   value = 3; break;
        case vte::terminal::modes::ECMA::eALWAYS_RESET: value = 4; break;
        default: assert(mode >= 0); value = m_modes_ecma.get(mode) ? 1 : 2; break;
        }

        _vte_debug_print(VTE_DEBUG_MODES,
                         "Reporting mode %d (%s) is %d\n",
                         param, m_modes_ecma.mode_to_cstring(mode),
                         value);

        reply(seq, VTE_REPLY_DECRPM_ECMA, {param, value});
}

void
VteTerminalPrivate::DECRQM_DEC(vte::parser::Sequence const& seq)
{
        /*
         * DECRQM_DEC - request-mode-dec
         * Same as DECRQM_ECMA but for DEC modes.
         *
         * References: VT525
         */

        auto const param = seq.collect1(0);
        auto const mode = m_modes_private.mode_from_param(param);

        int value;
        switch (mode) {
        case vte::terminal::modes::ECMA::eUNKNOWN:      value = 0; break;
        case vte::terminal::modes::ECMA::eALWAYS_SET:   value = 3; break;
        case vte::terminal::modes::ECMA::eALWAYS_RESET: value = 4; break;
        default: assert(mode >= 0); value = m_modes_private.get(mode) ? 1 : 2; break;
        }

        _vte_debug_print(VTE_DEBUG_MODES,
                         "Reporting private mode %d (%s) is %d\n",
                         param, m_modes_private.mode_to_cstring(mode),
                         value);

        reply(seq, VTE_REPLY_DECRPM_DEC, {param, value});
}

void
VteTerminalPrivate::DECRQPKFM(vte::parser::Sequence const& seq)
{
        /*
         * DECRQPKFM - request-program-key-free-memory
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECRQPSR(vte::parser::Sequence const& seq)
{
        /*
         * DECRQPSR - request-presentation-state-report
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECRQSS(vte::parser::Sequence const& seq)
{
        /*
         * DECRQSS - request selection or setting
         *
         * References: VT525
         */
}

void
VteTerminalPrivate::DECRQTSR(vte::parser::Sequence const& seq)
{
        /*
         * DECRQTSR - request-terminal-state-report
         *
         * References: VT525
         */

        if (seq.collect1(0) != 1)
                return;
}

void
VteTerminalPrivate::DECRQUPSS(vte::parser::Sequence const& seq)
{
        /*
         * DECRQUPSS - request-user-preferred-supplemental-set
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECRSPS(vte::parser::Sequence const& seq)
{
        /*
         * DECRSPS - restore presentation state
         *
         * References: VT525
         */
}

void
VteTerminalPrivate::DECRSTS(vte::parser::Sequence const& seq)
{
        /*
         * DECRSTS - restore terminal state
         *
         * References: VT525
         */
}

void
VteTerminalPrivate::DECSACE(vte::parser::Sequence const& seq)
{
        /*
         * DECSACE - select-attribute-change-extent
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECSASD(vte::parser::Sequence const& seq)
{
        /*
         * DECSASD - select-active-status-display
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECSC(vte::parser::Sequence const& seq)
{
        /*
         * DECSC - save-cursor
         * Save cursor and terminal state so it can be restored later on.
         * This stores:
         *   * Cursor position
         *   * SGR attributes
         *   * Charset designations for GL and GR
         *   * Wrap flag
         *   * DECOM state
         *   * Selective erase attribute
         *   * Any SS2 or SS3 sent
         *
         * References: VT525
         */
#if 0
        screen_save_state(screen, &screen->saved);
#endif

        save_cursor();
}

void
VteTerminalPrivate::DECSCA(vte::parser::Sequence const& seq)
{
        /*
         * DECSCA - select-character-protection-attribute
         *
         * Defaults:
         *   args[0]: 0
         *
         * References: VT525
         */
#if 0
        unsigned int mode = 0;

        if (seq->args[0] > 0)
                mode = seq->args[0];

        switch (mode) {
        case 0:
        case 2:
                screen->state.attr.protect = 0;
                break;
        case 1:
                screen->state.attr.protect = 1;
                break;
        }
#endif
}

void
VteTerminalPrivate::DECSCL(vte::parser::Sequence const& seq)
{
        /*
         * DECSCL - select-conformance-level
         * Select the terminal's operating level. The factory default is
         * level 4 (VT Level 4 mode, 7-bit controls).
         * When you change the conformance level, the terminal performs a hard
         * reset (RIS).
         *
         * @args[0] defines the conformance-level, valid values are:
         *   61: Level 1 (VT100)
         *   62: Level 2 (VT200)
         *   63: Level 3 (VT300)
         *   64: Level 4 (VT400)
         * @args[1] defines the 8bit-mode, valid values are:
         *    0: 8-bit controls
         *    1: 7-bit controls
         *    2: 8-bit controls (same as 0)
         *
         * If @args[0] is 61, then @args[1] is ignored and 7bit controls are
         * enforced.
         *
         * Defaults:
         *   args[0]: 64
         *   args[1]: 0
         */
#if 0
        unsigned int level = 64, bit = 0;

        if (seq->n_args > 0) {
                level = seq->args[0];
                if (seq->n_args > 1)
                        bit = seq->args[1];
        }

        vte_screen_hard_reset(screen);

        switch (level) {
        case 61:
                screen->conformance_level = VTE_CONFORMANCE_LEVEL_VT100;
                screen->flags |= VTE_FLAG_7BIT_MODE;
                break;
        case 62 ... 69:
                screen->conformance_level = VTE_CONFORMANCE_LEVEL_VT400;
                if (bit == 1)
                        screen->flags |= VTE_FLAG_7BIT_MODE;
                else
                        screen->flags &= ~VTE_FLAG_7BIT_MODE;
                break;
        }
#endif
}

void
VteTerminalPrivate::DECSCP(vte::parser::Sequence const& seq)
{
        /*
         * DECSCP - select-communication-port
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECSCPP(vte::parser::Sequence const& seq)
{
        /*
         * DECSCPP - select-columns-per-page
         * Select columns per page. The number of rows is unaffected by this.
         * @args[0] selectes the number of columns (width), DEC only defines 80
         * and 132, but we allow any integer here. 0 is equivalent to 80.
         * Page content is *not* cleared and the cursor is left untouched.
         * However, if the page is reduced in width and the cursor would be
         * outside the visible region, it's set to the right border. Newly added
         * cells are cleared. No data is retained outside the visible region.
         *
         * Defaults:
         *   args[0]: 0
         *
         * TODO: implement
         */
}

void
VteTerminalPrivate::DECSCS(vte::parser::Sequence const& seq)
{
        /*
         * DECSCS - select-communication-speed
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECSCUSR(vte::parser::Sequence const& seq)
{
        /*
         * DECSCUSR - set-cursor-style
         * This changes the style of the cursor. @args[0] can be one of:
         *   0, 1: blinking block
         *      2: steady block
         *      3: blinking underline
         *      4: steady underline
         *      5: blinking ibeam (XTERM)
         *      6: steady ibeam (XTERM)
         * Changing this setting does _not_ affect the cursor visibility itself.
         * Use DECTCEM for that.
         *
         * Defaults:
         *   args[0]: 0
         *
         * References: VT525 5–126
         *             XTERM
         */

        auto param = seq.collect1(0, 0);
        switch (param) {
        case 0 ... 6:
                set_cursor_style(VteCursorStyle(param));
                break;
        default:
                break;
        }
}

void
VteTerminalPrivate::DECSDDT(vte::parser::Sequence const& seq)
{
        /*
         * DECSDDT - select-disconnect-delay-time
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECSDPT(vte::parser::Sequence const& seq)
{
        /*
         * DECSDPT - select-digital-printed-data-type
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECSED(vte::parser::Sequence const& seq)
{
        /*
         * DECSED - selective-erase-in-display
         * This control function erases some or all of the erasable characters
         * in the display. DECSED can only erase characters defined as erasable
         * by the DECSCA control function. DECSED works inside or outside the
         * scrolling margins.
         *
         * @args[0] defines which regions are erased. If it is 0, all cells from
         * the cursor (inclusive) till the end of the display are erase. If it
         * is 1, all cells from the start of the display till the cursor
         * (inclusive) are erased. If it is 2, all cells are erased.
         *
         * Defaults:
         *   args[0]: 0
         */

        erase_in_display(seq);
}

void
VteTerminalPrivate::DECSEL(vte::parser::Sequence const& seq)
{
        /*
         * DECSEL - selective-erase-in-line
         * This control function erases some or all of the erasable characters
         * in a single line of text. DECSEL erases only those characters defined
         * as erasable by the DECSCA control function. DECSEL works inside or
         * outside the scrolling margins.
         *
         * @args[0] defines the region to be erased. If it is 0, all cells from
         * the cursor (inclusive) till the end of the line are erase. If it is
         * 1, all cells from the start of the line till the cursor (inclusive)
         * are erased. If it is 2, the whole line of the cursor is erased.
         *
         * Defaults:
         *   args[0]: 0
         */

        erase_in_line(seq);
}

void
VteTerminalPrivate::DECSERA(vte::parser::Sequence const& seq)
{
        /*
         * DECSERA - selective-erase-rectangular-area
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECSFC(vte::parser::Sequence const& seq)
{
        /*
         * DECSFC - select-flow-control
         *
         * Probably not worth implementing.
         */
}


void
VteTerminalPrivate::DECSIXEL(vte::parser::Sequence const& seq)
{
        /*
         * DECSIXEL - SIXEL graphics
         *
         * References: VT330
         */
}

void
VteTerminalPrivate::DECSKCV(vte::parser::Sequence const& seq)
{
        /*
         * DECSKCV - set-key-click-volume
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECSLCK(vte::parser::Sequence const& seq)
{
        /*
         * DECSLCK - set-lock-key-style
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECSLE(vte::parser::Sequence const& seq)
{
        /*
         * DECSLE - select-locator-events
         *
         * TODO: implement
         */
}

void
VteTerminalPrivate::DECSLPP(vte::parser::Sequence const& seq)
{
        /*
         * DECSLPP - set-lines-per-page
         * Set the number of lines used for the page. @args[0] specifies the
         * number of lines to be used. DEC only allows a limited number of
         * choices, however, we allow all integers. 0 is equivalent to 24.
         *
         * Defaults:
         *   args[0]: 0
         *
         * TODO: implement
         */
}

void
VteTerminalPrivate::DECSLRM_OR_SC(vte::parser::Sequence const& seq)
{
        /*
         * DECSLRM_OR_SC - set-left-and-right-margins or save-cursor
         *
         * TODO: Detect save-cursor and run it. DECSLRM is not worth
         *       implementing.
         *
         * References: VT525
         */

        save_cursor();
}

void
VteTerminalPrivate::DECSMBV(vte::parser::Sequence const& seq)
{
        /*
         * DECSMBV - set-margin-bell-volume
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECSMKR(vte::parser::Sequence const& seq)
{
        /*
         * DECSMKR - select-modifier-key-reporting
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECSNLS(vte::parser::Sequence const& seq)
{
        /*
         * DECSNLS - set-lines-per-screen
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECSPP(vte::parser::Sequence const& seq)
{
        /*
         * DECSPP - set-port-parameter
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECSPPCS(vte::parser::Sequence const& seq)
{
        /*
         * DECSPPCS - select-pro-printer-character-set
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECSPRTT(vte::parser::Sequence const& seq)
{
        /*
         * DECSPRTT - select-printer-type
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECSR(vte::parser::Sequence const& seq)
{
        /*
         * DECSR - secure-reset
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECSRFR(vte::parser::Sequence const& seq)
{
        /*
         * DECSRFR - select-refresh-rate
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECSSCLS(vte::parser::Sequence const& seq)
{
        /*
         * DECSSCLS - set-scroll-speed
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECSSDT(vte::parser::Sequence const& seq)
{
        /*
         * DECSSDT - select-status-display-line-type
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECSSL(vte::parser::Sequence const& seq)
{
        /*
         * DECSSL - select-setup-language
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECST8C(vte::parser::Sequence const& seq)
{
        /*
         * DECST8C - set-tab-at-every-8-columns
         * Clear the tab-ruler and reset it to a tab at every 8th column,
         * starting at 9 (though, setting a tab at 1 is fine as it has no
         * effect).
         *
         * References: VT525
         */

        if (seq.collect1(0) != 5)
                return;

        m_tabstops.reset(8);
        m_tabstops.unset(0);
}

void
VteTerminalPrivate::DECSTBM(vte::parser::Sequence const& seq)
{
        /*
         * DECSTBM - set-top-and-bottom-margins
         * This control function sets the top and bottom margins for the current
         * page. You cannot perform scrolling outside the margins.
         *
         * @args[0] defines the top margin, @args[1] defines the bottom margin.
         * The bottom margin must be lower than the top-margin.
         *
         * This call resets the cursor position to (1,1).
         *
         * Defaults:
         *   args[0]: 1
         *   args[1]: number of lines in screen
         *
         * References: VT525 5–149
         */
#if 0
        unsigned int top, bottom;

        top = 1;
        bottom = screen->page->height;

        if (seq->args[0] > 0)
                top = seq->args[0];
        if (seq->args[1] > 0)
                bottom = seq->args[1];

        if (top > screen->page->height)
                top = screen->page->height;
        if (bottom > screen->page->height)
                bottom = screen->page->height;

        if (top >= bottom ||
            top > screen->page->height ||
            bottom > screen->page->height) {
                top = 1;
                bottom = screen->page->height;
        }

        vte_page_set_scroll_region(screen->page, top - 1, bottom - top + 1);
        screen_cursor_clear_wrap(screen);
        screen_cursor_set(screen, 0, 0);
#endif

        int start, end;
        seq.collect(0, {&start, &end});

        /* Defaults */
        if (start <= 0)
                start = 1;
        if (end == -1)
                end = m_row_count;

        if (start > m_row_count ||
            end <= start) {
                m_scrolling_restricted = FALSE;
                home_cursor();
                return;
        }

        if (end > m_row_count)
                end = m_row_count;

	/* Set the right values. */
        m_scrolling_region.start = start - 1;
        m_scrolling_region.end = end - 1;
        m_scrolling_restricted = TRUE;
        if (m_scrolling_region.start == 0 &&
            m_scrolling_region.end == m_row_count - 1) {
		/* Special case -- run wild, run free. */
                m_scrolling_restricted = FALSE;
	} else {
		/* Maybe extend the ring -- bug 710483 */
                while (_vte_ring_next(m_screen->row_data) < m_screen->insert_delta + m_row_count)
                        _vte_ring_insert(m_screen->row_data, _vte_ring_next(m_screen->row_data));
	}

        home_cursor();
}

void
VteTerminalPrivate::DECSTR(vte::parser::Sequence const& seq)
{
        /*
         * DECSTR - soft-terminal-reset
         * Perform a soft reset to the default values.
         *
         * References: VT525
         */
#if 0
        vte_screen_soft_reset(screen);
#endif

	reset(false, false);
}

void
VteTerminalPrivate::DECSTRL(vte::parser::Sequence const& seq)
{
        /*
         * DECSTRL - set-transmit-rate-limit
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECSTUI(vte::parser::Sequence const& seq)
{
        /*
         * DECSTUI - set terminal unit ID
         *
         * References: VT525
         */
}

void
VteTerminalPrivate::DECSWBV(vte::parser::Sequence const& seq)
{
        /*
         * DECSWBV - set-warning-bell-volume
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECSWL(vte::parser::Sequence const& seq)
{
        /*
         * DECSWL - single-width-single-height-line
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECTID(vte::parser::Sequence const& seq)
{
        /*
         * DECTID - select-terminal-id
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECTME(vte::parser::Sequence const& seq)
{
        /*
         * DECTME - terminal-mode-emulation
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECTST(vte::parser::Sequence const& seq)
{
        /*
         * DECTST - invoke-confidence-test
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::DECUDK(vte::parser::Sequence const& seq)
{
        /*
         * DECUDK - user define keys
         *
         * References: VT525
         */
}

void
VteTerminalPrivate::DL(vte::parser::Sequence const& seq)
{
        /*
         * DL - delete-line
         *
         * Defaults:
         *   args[0]: 1
         *
         * References: ECMA-48 § 8.3.32
         */
#if 0
        unsigned int num = 1;

        if (seq->args[0] > 0)
                num = seq->args[0];

        vte_page_delete_lines(screen->page,
                                 screen->state.cursor_y,
                                 num,
                                 &screen->state.attr,
                                 screen->age);
#endif

        auto const count = seq.collect1(0, 1);
        delete_lines(count);
}

void
VteTerminalPrivate::DOCS(vte::parser::Sequence const& seq)
{
        /*
         * DOCS - designate other coding systyem
         *
         * References: ECMA-35 § 15.4
         *             ISO 2375 IR
         *
         * TODO: implement (bug #787228)
         */
}

void
VteTerminalPrivate::DSR_ECMA(vte::parser::Sequence const& seq)
{
        /*
         * DSR_ECMA - Device Status Report
         *
         * Reports status, or requests a status report.
         *
         * Defaults:
         *   arg[0]: 0
         *
         * References: ECMA-48 § 8.3.35
         */

        switch (seq.collect1(0)) {
        case -1:
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
                /* This is a status report */
                break;

        case 5:
                /* Request operating status report.
                 * Reply: DSR
                 *   @arg[0]: status
                 *     0 = ok
                 *     3 = malfunction
                 */
                reply(seq, VTE_REPLY_DSR, {0});
                break;

        case 6:
                /* Request extended cursor position report
                 * Reply: CPR
                 *   @arg[0]: line
                 *   @arg[1]: column
                 */
                vte::grid::row_t rowval, origin, rowmax;
                if (m_modes_private.DEC_ORIGIN() &&
                    m_scrolling_restricted) {
                        origin = m_scrolling_region.start;
                        rowmax = m_scrolling_region.end;
                } else {
                        origin = 0;
                        rowmax = m_row_count - 1;
                }
                // FIXMEchpe this looks wrong. shouldn't this first clamp to origin,rowmax and *then* subtract origin?
                rowval = m_screen->cursor.row - m_screen->insert_delta - origin;
                rowval = CLAMP(rowval, 0, rowmax);

                reply(seq, VTE_REPLY_CPR,
                      {int(rowval + 1), int(CLAMP(m_screen->cursor.col + 1, 1, m_column_count))});
                break;

        default:
                break;
        }
}

void
VteTerminalPrivate::DSR_DEC(vte::parser::Sequence const& seq)
{
        /*
         * DSR_DEC - device-status-report-dec
         *
         * Reports status, or requests a status report.
         *
         * Defaults:
         *   arg[0]: 0
         *
         * References: VT525 5–173
         *             VT330
         *             XTERM
         */

        switch (seq.collect1(0)) {
        case 6:
                /* Request extended cursor position report
                 * Reply: DECXCPR
                 *   @arg[0]: line
                 *   @arg[1]: column
                 *   @arg[2]: page
                 *     Always report page 1 here (per XTERM source code).
                 */
                vte::grid::row_t rowval, origin, rowmax;
                if (m_modes_private.DEC_ORIGIN() &&
                    m_scrolling_restricted) {
                        origin = m_scrolling_region.start;
                        rowmax = m_scrolling_region.end;
                } else {
                        origin = 0;
                        rowmax = m_row_count - 1;
                }
                // FIXMEchpe this looks wrong. shouldn't this first clamp to origin,rowmax and *then* subtract origin?
                rowval = m_screen->cursor.row - m_screen->insert_delta - origin;
                rowval = CLAMP(rowval, 0, rowmax);

                reply(seq, VTE_REPLY_DECXCPR,
                      {int(rowval + 1), int(CLAMP(m_screen->cursor.col + 1, 1, m_column_count)), 1});
                break;

        case 15:
                /* Request printer port report
                 * Reply: DECDSR
                 *   @arg[0]: status
                 *     10 = printer ready
                 *     11 = printer not ready
                 *     13 = no printer
                 *     18 = printer busy
                 *     19 = printer assigned to another session
                 */
                reply(seq, VTE_REPLY_DECDSR, {13});
                break;

        case 25:
                /* Request user-defined keys report
                 * Reply: DECDSR
                 *   @arg[0]: locked status
                 *      20 = UDK unlocked
                 *      21 = UDK locked
                 *
                 * Since we don't do UDK, we report them as locked.
                 */
                reply(seq, VTE_REPLY_DECDSR, {21});
                break;

        case 26:
                /* Request keyboard report
                 * Reply: DECDSR
                 *   @arg[0]: 27
                 *   @arg[1]: Keyboard language
                 *     0 = undetermined
                 *     1..40
                 *
                 *   @arg[2]: Keyboard status
                 *     0 = ready
                 *     3 = no keyboard
                 *     8 = keyboard busy
                 *
                 *   @arg[3]: Keyboard type
                 *     0 = LK201 (XTERM response)
                 *     4 = LK411
                 *     5 = PCXAL
                 */
                reply(seq, VTE_REPLY_DECDSR, {27, 0, 0, 5});
                break;

        case 53:
                /* XTERM alias for 55 */
                /* [[fallthrough]]; */
        case 55:
                /* Request locator status report
                 * Reply: DECDSR
                 *   @arg[0]: status
                 *     50 = locator ready
                 *     53 = no locator
                 *
                 * Since we don't implement the DEC locator mode,
                 * we reply with 53.
                 */
                reply(seq, VTE_REPLY_DECDSR, {53});
                break;

        case 56:
                /* Request locator type report
                 * Reply: DECDSR
                 *   @arg[0]: 57
                 *   @arg[1]: status
                 *     0 = unknown
                 *     1 = mouse
                 *
                 * Since we don't implement the DEC locator mode,
                 * we reply with 0.
                 */
                reply(seq, VTE_REPLY_DECDSR, {57, 0});
                break;

        case 62:
                /* Request macro space report
                 * Reply: DECMSR
                 *   @arg[0]: floor((number of bytes available) / 16); we report 0
                 */
                reply(seq, VTE_REPLY_DECMSR, {0});
                break;

        case 63:
                /* Request memory checksum report
                 * Reply: DECCKSR
                 *   @arg[0]: PID
                 *   DATA: the checksum as a 4-digit hex number
                 *
                 * Reply with a dummy checksum.
                 */
                reply(seq, VTE_REPLY_DECCKSR, {seq.collect1(1)}, "0000");
                break;

        case 75:
                /* Request data integrity report
                 * Reply: DECDSR
                 *   @arg[0]: status
                 *     70 = no error, no power loss, no communication errors
                 *     71 = malfunction or communication error
                 *     73 = no data loss since last power-up
                 */
                reply(seq, VTE_REPLY_DECDSR, {70});
                break;

        case 85:
                /* Request multi-session status report
                 * Reply: DECDSR
                 *   @arg[0]: status
                 *     ...
                 *     83 = not configured
                 */
                reply(seq, VTE_REPLY_DECDSR, {83});
                break;

        default:
                break;
        }
}

void
VteTerminalPrivate::ECH(vte::parser::Sequence const& seq)
{
        /*
         * ECH - erase-character
         *
         * Defaults:
         *   args[0]: 1
         *
         * References: ECMA-48 § 8.3.38
         */
#if 0
        unsigned int num = 1;

        if (seq->args[0] > 0)
                num = seq->args[0];

        vte_page_erase(screen->page,
                          screen->state.cursor_x, screen->state.cursor_y,
                          screen->state.cursor_x + num, screen->state.cursor_y,
                          &screen->state.attr, screen->age, false);
#endif

        /* Erase characters starting at the cursor position (overwriting N with
         * spaces, but not moving the cursor). */

        // FIXMEchpe limit to column_count - cursor.x ?
        auto const count = seq.collect1(0, 1, 1, int(65535));
        erase_characters(count);
}

void
VteTerminalPrivate::ED(vte::parser::Sequence const& seq)
{
        /*
         * ED - erase-in-display
         *
         * Defaults:
         *   args[0]: 0
         */

        erase_in_display(seq);
}

void
VteTerminalPrivate::EL(vte::parser::Sequence const& seq)
{
        /*
         * EL - erase-in-line
         *
         * Defaults:
         *   args[0]: 0
         */

        erase_in_line(seq);
}

void
VteTerminalPrivate::ENQ(vte::parser::Sequence const& seq)
{
        /*
         * ENQ - enquiry
         * Transmit the answerback-string. If none is set, do nothing.
         *
         * References: ECMA-48 § 8.3.44
         */
#if 0
        if (screen->answerback)
                return screen_write(screen,
                                    screen->answerback,
                                    strlen(screen->answerback));
#endif

        /* No-op for security reasons */
}

void
VteTerminalPrivate::EPA(vte::parser::Sequence const& seq)
{
        /*
         * EPA - end-of-guarded-area
         *
         * TODO: What is this?
         */
}

void
VteTerminalPrivate::FF(vte::parser::Sequence const& seq)
{
        /*
         * FF - form-feed
         * This causes the cursor to jump to the next line. It is treated the
         * same as LF.
         *
         * References: ECMA-48 § 8.3.51
         */

        LF(seq);
}

void
VteTerminalPrivate::GnDm(vte::parser::Sequence const& seq)
{
        /*
         * GnDm - Gn-designate 9m-charset
         *
         * Designate character sets to G-sets.
         *
         * References: ECMA-35 § 14.3
         *             ISO 2375 IR
         */

        /* Since we don't implement ISO-2022 anymore, we can mostly ignore this. */

        VteCharacterReplacement replacement;
        switch (seq.charset()) {
        case VTE_CHARSET_DEC_SPECIAL_GRAPHIC:
                /* Some characters replaced by line drawing characters.
                 * This is still used by ncurses :-(
                 */
                replacement = VTE_CHARACTER_REPLACEMENT_LINE_DRAWING;
                break;

        case VTE_CHARSET_BRITISH_NRCS:
                /* # is converted to £ */
                /* FIXME: Remove this */
                replacement = VTE_CHARACTER_REPLACEMENT_BRITISH;
                break;

        /* FIXME: are any of the other charsets still useful? */
        default:
                replacement = VTE_CHARACTER_REPLACEMENT_NONE;
                break;
        }

        unsigned int slot = seq.slot();
        if (slot >= G_N_ELEMENTS(m_character_replacements))
                return;

        m_character_replacements[slot] = replacement;
}

void
VteTerminalPrivate::GnDMm(vte::parser::Sequence const& seq)
{
        /*
         * GnDm - Gn-designate multibyte 9m-charset
         *
         * Designate multibyte character sets to G-sets.
         *
         * References: ECMA-35 § 14.3
         *             ISO 2375 IR
         */

        /* Since we don't implement ISO-2022 anymore, we can ignore this */
}

void
VteTerminalPrivate::HPA(vte::parser::Sequence const& seq)
{
        /*
         * HPA - horizontal-position-absolute
         * HPA causes the active position to be moved to the n-th horizontal
         * position of the active line. If an attempt is made to move the active
         * position past the last position on the line, then the active position
         * stops at the last position on the line.
         *
         * @args[0] defines the horizontal position. 0 is treated as 1.
         *
         * Note: This does the same as CHA
         *
         * Defaults:
         *   args[0]: 1
         *
         * References: ECMA-48 § 8.3.57
         */

#if 0
        unsigned int num = 1;

        if (seq->args[0] > 0)
                num = seq->args[0];

        screen_cursor_clear_wrap(screen);
        screen_cursor_set(screen, num - 1, screen->state.cursor_y);
#endif

        auto value = seq.collect1(0, 1, 1, m_column_count);
        set_cursor_column1(value);
}

void
VteTerminalPrivate::HPR(vte::parser::Sequence const& seq)
{
        /*
         * HPR - horizontal-position-relative
         * HPR causes the active position to be moved to the n-th following
         * horizontal position of the active line. If an attempt is made to move
         * the active position past the last position on the line, then the
         * active position stops at the last position on the line.
         *
         * @args[0] defines the horizontal position. 0 is treated as 1.
         *
         * Defaults:
         *   args[0]: 1
         */
#if 0
        unsigned int num = 1;

        if (seq->args[0] > 0)
                num = seq->args[0];

        screen_cursor_clear_wrap(screen);
        screen_cursor_right(screen, num);
#endif
}

void
VteTerminalPrivate::HT(vte::parser::Sequence const& seq)
{
        /*
         * HT - horizontal-tab
         * Moves the cursor to the next tab stop. If there are no more tab
         * stops, the cursor moves to the right margin. HT does not cause text
         * to auto wrap.
         *
         * References: ECMA-48 § 8.3.60
         */
#if 0
        screen_cursor_clear_wrap(screen);
#endif

        move_cursor_tab_forward();
}

void
VteTerminalPrivate::HTS(vte::parser::Sequence const& seq)
{
        /*
         * HTS - horizontal-tab-set
         * XXX
         *
         * Executing an HTS does not effect the other horizontal tab stop
         * settings.
         *
         * References: ECMA-48 § 8.3.62
         */

        m_tabstops.set(m_screen->cursor.col);
}

void
VteTerminalPrivate::HVP(vte::parser::Sequence const& seq)
{
        /*
         * HVP - horizontal-and-vertical-position
         * XXX
         *
         * Defaults:
         *   args[0]: 1
         *   args[1]: 1
         *
         * References: ECMA-48 FIXME
         *             VT525
         */

        CUP(seq);
}

void
VteTerminalPrivate::ICH(vte::parser::Sequence const& seq)
{
        /*
         * ICH - insert-character
         * XXX
         *
         * Defaults:
         *   args[0]: 1
         *
         * References: ECMA-48 §8.3.64
         */
#if 0
        unsigned int num = 1;

        if (seq->args[0] > 0)
                num = seq->args[0];

        screen_cursor_clear_wrap(screen);
        vte_page_insert_cells(screen->page,
                                 screen->state.cursor_x,
                                 screen->state.cursor_y,
                                 num,
                                 &screen->state.attr,
                                 screen->age);
#endif

        auto const count = seq.collect1(0, 1, 1, int(m_column_count - m_screen->cursor.col));

        /* TODOegmont: Insert them in a single run, so that we call cleanup_fragments only once. */
        for (auto i = 0; i < count; i++)
                insert_blank_character();
}

void
VteTerminalPrivate::IL(vte::parser::Sequence const& seq)
{
        /*
         * IL - insert-line
         * XXX
         *
         * Defaults:
         *   args[0]: 1
         *
         * References: ECMA-48 § 8.3.67
         */
#if 0
        unsigned int num = 1;

        if (seq->args[0] > 0)
                num = seq->args[0];

        screen_cursor_clear_wrap(screen);
        vte_page_insert_lines(screen->page,
                                 screen->state.cursor_y,
                                 num,
                                 &screen->state.attr,
                                 screen->age);
#endif

        auto const count = seq.collect1(0, 1);
        insert_lines(count);
}

void
VteTerminalPrivate::IND(vte::parser::Sequence const& seq)
{
        /*
         * IND - index - DEPRECATED
         *
         * References: ECMA-48 § F.8.2
         */

        LF(seq);
}

void
VteTerminalPrivate::IRR(vte::parser::Sequence const& seq)
{
        /*
         * IRR - identify-revised-registration
         *
         * References: ECMA-35 § 14.5
         *
         * Probably not worth implementing.
         */

        /* Since we don't implement ISO-2022 anymore, we can ignore this */
}

void
VteTerminalPrivate::LF(vte::parser::Sequence const& seq)
{
        /*
         * LF - line-feed
         * Causes a line feed or a new line operation, depending on the setting
         * of line feed/new line mode.
         *
         * References: ECMA-48 § 8.3.74
         */

#if 0
        screen_cursor_down(screen, 1, true);
        if (screen->flags & VTE_FLAG_NEWLINE_MODE)
                screen_cursor_left(screen, screen->state.cursor_x);
#endif

        line_feed();
}

void
VteTerminalPrivate::LS1R(vte::parser::Sequence const& seq)
{
        /*
         * LS1R - locking-shift-1-right
         * Map G1 into GR.
         *
         * References: ECMA-35 § 9.3.2
         *             ECMA-48 § 8.3.77
         */
#if 0
        screen->state.gr = &screen->g1;
#endif
}

void
VteTerminalPrivate::LS2(vte::parser::Sequence const& seq)
{
        /*
         * LS2 - locking-shift-2
         * Map G2 into GL.
         *
         * References: ECMA-35 § 9.3.1
         *             ECMA-48 § 8.3.78
         */
#if 0
        screen->state.gl = &screen->g2;
#endif
}

void
VteTerminalPrivate::LS2R(vte::parser::Sequence const& seq)
{
        /*
         * LS2R - locking-shift-2-right
         * Map G2 into GR.
         *
         * References: ECMA-35 § 9.3.2
         *             ECMA-48 § 8.3.79
         */
#if 0
        screen->state.gr = &screen->g2;
#endif
}

void
VteTerminalPrivate::LS3(vte::parser::Sequence const& seq)
{
        /*
         * LS3 - locking-shift-3
         * Map G3 into GL.
         *
         * References: ECMA-35 § 9.3.1
         *             ECMA-48 § 8.3.80
         */

#if 0
        screen->state.gl = &screen->g3;
#endif
}

void
VteTerminalPrivate::LS3R(vte::parser::Sequence const& seq)
{
        /*
         * LS3R - locking-shift-3-right
         * Map G3 into GR.
         *
         * References: ECMA-35 § 9.3.2
         *             ECMA-48 § 8.3.81
         */
#if 0
        screen->state.gr = &screen->g3;
#endif
}

void
VteTerminalPrivate::MC_ANSI(vte::parser::Sequence const& seq)
{
        /*
         * MC_ANSI - media-copy-ansi
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::MC_DEC(vte::parser::Sequence const& seq)
{
        /*
         * MC_DEC - media-copy-dec
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::NEL(vte::parser::Sequence const& seq)
{
        /*
         * NEL - next-line
         * XXX
         *
         * References: ECMA-48 § 8.3.86
         */
#if 0
        screen_cursor_clear_wrap(screen);
        screen_cursor_down(screen, 1, true);
        screen_cursor_set(screen, 0, screen->state.cursor_y);
#endif

        set_cursor_column(0);
        cursor_down(true);
}

void
VteTerminalPrivate::NP(vte::parser::Sequence const& seq)
{
        /*
         * NP - next-page
         * XXX
         *
         * Defaults:
         *   args[0]: 1
         *
         * Probably not worth implementing. We only support a single page.
         */
}

void
VteTerminalPrivate::NUL(vte::parser::Sequence const& seq)
{
        /*
         */
}

void
VteTerminalPrivate::OSC(vte::parser::Sequence const& seq)
{
        /*
         * OSC - operating system command
         *
         * References: ECMA-48 § 8.3.89
         *             XTERM
         */

        /* Our OSC have the format
         *   OSC number ; rest of string ST
         * where the rest of the string may or may not contain more semicolons.
         *
         * First, extract the number.
         */

        auto const str = seq.string_utf8();
        vte::parser::StringTokeniser tokeniser{str, ';'};
        auto it = tokeniser.cbegin();
        int osc;
        if (!it.number(osc))
                return;

        auto const cend = tokeniser.cend();
        ++it; /* could now be cend */

        switch (osc) {
        case VTE_OSC_VTECWF:
                set_current_file_uri(seq, it, cend);
                break;

        case VTE_OSC_VTECWD:
                set_current_directory_uri(seq, it, cend);
                break;

        case VTE_OSC_VTEHYPER:
                set_current_hyperlink(seq, it, cend);
                break;

        case -1: /* default */
        case VTE_OSC_XTERM_SET_WINDOW_AND_ICON_TITLE:
        case VTE_OSC_XTERM_SET_WINDOW_TITLE: {
                /* Only sets window title; icon title is not supported */
                std::string title;
                if (it != cend)
                        title = it.string_remaining();
                m_window_title_pending.swap(title);
                m_window_title_changed = true;
                break;
        }

        case VTE_OSC_XTERM_SET_COLOR:
                set_color(seq, it, cend);
                break;

        case VTE_OSC_XTERM_SET_COLOR_TEXT_FG:
                set_special_color(seq, it, cend, VTE_DEFAULT_FG, -1, osc);
                break;

        case VTE_OSC_XTERM_SET_COLOR_TEXT_BG:
                set_special_color(seq, it, cend, VTE_DEFAULT_BG, -1, osc);
                break;

        case VTE_OSC_XTERM_SET_COLOR_CURSOR_BG:
                set_special_color(seq, it, cend, VTE_CURSOR_BG, VTE_DEFAULT_FG, osc);
                break;

        case VTE_OSC_XTERM_SET_COLOR_HIGHLIGHT_BG:
                set_special_color(seq, it, cend, VTE_HIGHLIGHT_BG, VTE_DEFAULT_FG, osc);
                break;

        case VTE_OSC_XTERM_SET_COLOR_HIGHLIGHT_FG:
                set_special_color(seq, it, cend, VTE_HIGHLIGHT_FG, VTE_DEFAULT_BG, osc);
                break;

        case VTE_OSC_XTERM_RESET_COLOR:
                reset_color(seq, it, cend);
                break;

        case VTE_OSC_XTERM_RESET_COLOR_TEXT_FG:
                reset_color(VTE_DEFAULT_FG, VTE_COLOR_SOURCE_ESCAPE);
                break;

        case VTE_OSC_XTERM_RESET_COLOR_TEXT_BG:
                reset_color(VTE_DEFAULT_BG, VTE_COLOR_SOURCE_ESCAPE);
                break;

        case VTE_OSC_XTERM_RESET_COLOR_CURSOR_BG:
                reset_color(VTE_CURSOR_BG, VTE_COLOR_SOURCE_ESCAPE);
                break;

        case VTE_OSC_XTERM_RESET_COLOR_HIGHLIGHT_BG:
                reset_color(VTE_HIGHLIGHT_BG, VTE_COLOR_SOURCE_ESCAPE);
                break;

        case VTE_OSC_XTERM_RESET_COLOR_HIGHLIGHT_FG:
                reset_color(VTE_HIGHLIGHT_FG, VTE_COLOR_SOURCE_ESCAPE);
                break;

        case VTE_OSC_XTERM_SET_ICON_TITLE:
        case VTE_OSC_XTERM_SET_XPROPERTY:
        case VTE_OSC_XTERM_SET_COLOR_SPECIAL:
        case VTE_OSC_XTERM_SET_COLOR_MOUSE_CURSOR_FG:
        case VTE_OSC_XTERM_SET_COLOR_MOUSE_CURSOR_BG:
        case VTE_OSC_XTERM_SET_COLOR_TEK_FG:
        case VTE_OSC_XTERM_SET_COLOR_TEK_BG:
        case VTE_OSC_XTERM_SET_COLOR_TEK_CURSOR:
        case VTE_OSC_XTERM_LOGFILE:
        case VTE_OSC_XTERM_SET_FONT:
        case VTE_OSC_XTERM_SET_XSELECTION:
        case VTE_OSC_XTERM_RESET_COLOR_SPECIAL:
        case VTE_OSC_XTERM_SET_COLOR_MODE:
        case VTE_OSC_XTERM_RESET_COLOR_MOUSE_CURSOR_FG:
        case VTE_OSC_XTERM_RESET_COLOR_MOUSE_CURSOR_BG:
        case VTE_OSC_XTERM_RESET_COLOR_TEK_FG:
        case VTE_OSC_XTERM_RESET_COLOR_TEK_BG:
        case VTE_OSC_XTERM_RESET_COLOR_TEK_CURSOR:
        case VTE_OSC_EMACS_51:
        case VTE_OSC_ITERM2_133:
        case VTE_OSC_ITERM2_1337:
        case VTE_OSC_ITERM2_GROWL:
        case VTE_OSC_KONSOLE_30:
        case VTE_OSC_KONSOLE_31:
        case VTE_OSC_RLOGIN_SET_KANJI_MODE:
        case VTE_OSC_RLOGIN_SPEECH:
        case VTE_OSC_RXVT_SET_BACKGROUND_PIXMAP:
        case VTE_OSC_RXVT_SET_COLOR_FG:
        case VTE_OSC_RXVT_SET_COLOR_BG:
        case VTE_OSC_RXVT_DUMP_SCREEN:
        case VTE_OSC_URXVT_SET_LOCALE:
        case VTE_OSC_URXVT_VERSION:
        case VTE_OSC_URXVT_SET_COLOR_TEXT_ITALIC:
        case VTE_OSC_URXVT_SET_COLOR_TEXT_BOLD:
        case VTE_OSC_URXVT_SET_COLOR_UNDERLINE:
        case VTE_OSC_URXVT_SET_COLOR_BORDER:
        case VTE_OSC_URXVT_SET_FONT:
        case VTE_OSC_URXVT_SET_FONT_BOLD:
        case VTE_OSC_URXVT_SET_FONT_ITALIC:
        case VTE_OSC_URXVT_SET_FONT_BOLD_ITALIC:
        case VTE_OSC_URXVT_VIEW_UP:
        case VTE_OSC_URXVT_VIEW_DOWN:
        case VTE_OSC_URXVT_EXTENSION:
        case VTE_OSC_YF_RQGWR:
        default:
                break;
        }
}

void
VteTerminalPrivate::PP(vte::parser::Sequence const& seq)
{
        /*
         * PP - preceding-page
         * XXX
         *
         * Defaults:
         *   args[0]: 1
         *
         * Probably not worth implementing. We only support a single page.
         */
}

void
VteTerminalPrivate::PPA(vte::parser::Sequence const& seq)
{
        /*
         * PPA - page-position-absolute
         * XXX
         *
         * Defaults:
         *   args[0]: 1
         *
         * Probably not worth implementing. We only support a single page.
         */
}

void
VteTerminalPrivate::PPB(vte::parser::Sequence const& seq)
{
        /*
         * PPB - page-position-backward
         * XXX
         *
         * Defaults:
         *   args[0]: 1
         *
         * Probably not worth implementing. We only support a single page.
         */
}

void
VteTerminalPrivate::PPR(vte::parser::Sequence const& seq)
{
        /*
         * PPR - page-position-relative
         * XXX
         *
         * Defaults:
         *   args[0]: 1
         *
         * Probably not worth implementing. We only support a single page.
         */
}

void
VteTerminalPrivate::RC(vte::parser::Sequence const& seq)
{
        /*
         * RC - restore-cursor
         */

#if 0
        screen_DECRC(screen, seq);
#endif
}

void
VteTerminalPrivate::REP(vte::parser::Sequence const& seq)
{
        /*
         * REP - repeat
         * Repeat the preceding graphics-character the given number of times.
         * @args[0] specifies how often it shall be repeated. 0 is treated as 1.
         *
         * Defaults:
         *   args[0]: 1
         *
         * References: ECMA-48 § 8.3.103
         */

        if (m_last_graphic_character == 0)
                return;

        auto const count = seq.collect1(0, 1, 1, int(m_column_count - m_screen->cursor.col));

        // FIXMEchpe insert in one run so we only clean up fragments once
        for (auto i = 0; i < count; i++)
                insert_char(m_last_graphic_character, false, true);
}

void
VteTerminalPrivate::RI(vte::parser::Sequence const& seq)
{
        /*
         * RI - reverse-index
         * Moves the cursor up one line in the same column. If the cursor is at
         * the top margin, the page scrolls down.
         *
         * References: ECMA-48 § 8.3.104
         */
#if 0
        screen_cursor_up(screen, 1, true);
#endif

        ensure_cursor_is_onscreen();

        vte::grid::row_t start, end;
        if (m_scrolling_restricted) {
                start = m_scrolling_region.start + m_screen->insert_delta;
                end = m_scrolling_region.end + m_screen->insert_delta;
	} else {
                start = m_screen->insert_delta;
                end = start + m_row_count - 1;
	}

        if (m_screen->cursor.row == start) {
		/* If we're at the top of the scrolling region, add a
		 * line at the top to scroll the bottom off. */
		ring_remove(end);
		ring_insert(start, true);
		/* Update the display. */
		scroll_region(start, end - start + 1, 1);
                invalidate_cells(0, m_column_count,
                                 start, 2);
	} else {
		/* Otherwise, just move the cursor up. */
                m_screen->cursor.row--;
	}
	/* Adjust the scrollbars if necessary. */
        adjust_adjustments();
	/* We modified the display, so make a note of it. */
        m_text_modified_flag = TRUE;
}

void
VteTerminalPrivate::RIS(vte::parser::Sequence const& seq)
{
        /*
         * RIS - reset-to-initial-state
         * XXX
         *
         * References: ECMA-48 § 8.3.105
         */

#if 0
        vte_screen_hard_reset(screen);
#endif

	reset(true, true);
}

void
VteTerminalPrivate::RM_ECMA(vte::parser::Sequence const& seq)
{
        /*
         * RM_ECMA - reset-mode-ecma
         *
         * Defaults: none
         *
         * References: ECMA-48 § 8.3.106
         */

        set_mode_ecma(seq, false);
}

void
VteTerminalPrivate::RM_DEC(vte::parser::Sequence const& seq)
{
        /*
         * RM_DEC - reset-mode-dec
         * This is the same as RM_ECMA but for DEC modes.
         *
         * Defaults: none
         *
         * References: VT525
         */

        set_mode_private(seq, false);
}

void
VteTerminalPrivate::SD(vte::parser::Sequence const& seq)
{
        /*
         * SD - scroll-down
         * XXX
         *
         * Defaults:
         *   args[0]: 1
         *
         * References: ECMA-48 § 8.3.113
         */
#if 0
        unsigned int num = 1;

        if (seq->args[0] > 0)
                num = seq->args[0];

        vte_page_scroll_down(screen->page,
                                num,
                                &screen->state.attr,
                                screen->age,
                                NULL);
#endif

        /* Scroll the text down N lines, but don't move the cursor. */
        auto value = std::max(seq.collect1(0, 1), int(1));
        scroll_text(value);
}

void
VteTerminalPrivate::SD_OR_XTERM_IHMT(vte::parser::Sequence const& seq)
{
        /*
         * There's a conflict between SD and XTERM IHMT that we
         * have to resolve by checking the parameter count.
         * XTERM_IHMT needs exactly 5 arguments, SD takes 0 or 1.
         */
        if (seq.size_final() <= 1)
                SD(seq);
        else
                XTERM_IHMT(seq);
}

void
VteTerminalPrivate::SGR(vte::parser::Sequence const& seq)
{
        /*
         * SGR - select-graphics-rendition
         */
        auto const n_params = seq.size();

	/* If we had no parameters, default to the defaults. */
	if (n_params == 0) {
                reset_default_attributes(false);
                return;
	}

        for (unsigned int i = 0; i < n_params; i = seq.next(i)) {
                auto const param = seq.param(i);
                switch (param) {
                case -1:
                case 0:
                        reset_default_attributes(false);
                        break;
                case 1:
                        m_defaults.attr.set_bold(true);
                        break;
                case 2:
                        m_defaults.attr.set_dim(true);
                        break;
                case 3:
                        m_defaults.attr.set_italic(true);
                        break;
                case 4: {
                        unsigned int v = 1;
                        /* If we have a subparameter, get it */
                        if (seq.param_nonfinal(i)) {
                                v = seq.param(i + 1, 1, 0, 3);
                        }
                        m_defaults.attr.set_underline(v);
                        break;
                }
                case 5:
                        m_defaults.attr.set_blink(true);
                        break;
                case 7:
                        m_defaults.attr.set_reverse(true);
                        break;
                case 8:
                        m_defaults.attr.set_invisible(true);
                        break;
                case 9:
                        m_defaults.attr.set_strikethrough(true);
                        break;
                case 21:
                        m_defaults.attr.set_underline(2);
                        break;
                case 22: /* ECMA 48. */
                        m_defaults.attr.unset(VTE_ATTR_BOLD_MASK | VTE_ATTR_DIM_MASK);
                        break;
                case 23:
                        m_defaults.attr.set_italic(false);
                        break;
                case 24:
                        m_defaults.attr.set_underline(0);
                        break;
                case 25:
                        m_defaults.attr.set_blink(false);
                        break;
                case 27:
                        m_defaults.attr.set_reverse(false);
                        break;
                case 28:
                        m_defaults.attr.set_invisible(false);
                        break;
                case 29:
                        m_defaults.attr.set_strikethrough(false);
                        break;
                case 30 ... 37:
                        m_defaults.attr.set_fore(VTE_LEGACY_COLORS_OFFSET + (param - 30));
                        break;
                case 38: {
                        uint32_t fore;
                        if (G_LIKELY((seq_parse_sgr_color<8, 8, 8>(seq, i, fore))))
                                m_defaults.attr.set_fore(fore);
                        break;
                }
                case 39:
                        /* default foreground */
                        m_defaults.attr.set_fore(VTE_DEFAULT_FG);
                        break;
                case 40 ... 47:
                        m_defaults.attr.set_back(VTE_LEGACY_COLORS_OFFSET + (param - 40));
                        break;
                case 48: {
                        uint32_t back;
                        if (G_LIKELY((seq_parse_sgr_color<8, 8, 8>(seq, i, back))))
                                m_defaults.attr.set_back(back);
                        break;
                }
                case 49:
                        /* default background */
                        m_defaults.attr.set_back(VTE_DEFAULT_BG);
                        break;
                case 53:
                        m_defaults.attr.set_overline(true);
                        break;
                case 55:
                        m_defaults.attr.set_overline(false);
                        break;
                case 58: {
                        uint32_t deco;
                        if (G_LIKELY((seq_parse_sgr_color<4, 5, 4>(seq, i, deco))))
                                m_defaults.attr.set_deco(deco);
                        break;
                }
                case 59:
                        /* default decoration color, that is, same as the cell's foreground */
                        m_defaults.attr.set_deco(VTE_DEFAULT_FG);
                        break;
                case 90 ... 97:
                        m_defaults.attr.set_fore(VTE_LEGACY_COLORS_OFFSET + (param - 90) +
                                                 VTE_COLOR_BRIGHT_OFFSET);
                        break;
                case 100 ... 107:
                        m_defaults.attr.set_back(VTE_LEGACY_COLORS_OFFSET + (param - 100) +
                                                 VTE_COLOR_BRIGHT_OFFSET);
                        break;
                }
        }

	/* Save the new colors. */
        m_color_defaults.attr.copy_colors(m_defaults.attr);
        m_fill_defaults.attr.copy_colors(m_defaults.attr);
}

void
VteTerminalPrivate::SI(vte::parser::Sequence const& seq)
{
        /*
         * SI - shift-in
         * Map G0 into GL.
         *
         * References: ECMA-35 § 9.3.1
         *             ECMA-48 § 8.3.119
         */
#if 0
        screen->state.gl = &screen->g0;
#endif

        set_character_replacement(0);
}

void
VteTerminalPrivate::SM_ECMA(vte::parser::Sequence const& seq)
{
        /*
         * SM_ECMA - set-mode-ecma
         *
         * Defaults: none
         *
         * References: ECMA-48 § 8.3.125
         */

        set_mode_ecma(seq, true);
}

void
VteTerminalPrivate::SM_DEC(vte::parser::Sequence const& seq)
{
        /*
         * SM_DEC - set-mode-dec
         * This is the same as SM_ECMA but for DEC modes.
         *
         * Defaults: none
         *
         * References: VT525
         */

        set_mode_private(seq, true);
}

void
VteTerminalPrivate::SO(vte::parser::Sequence const& seq)
{
        /*
         * SO - shift-out
         * Map G1 into GL.
         *
         * References: ECMA-35 § 9.3.1
         *             ECMA-48 § 8.3.126
         */
#if 0
        screen->state.gl = &screen->g1;
#endif

        set_character_replacement(1);
}

void
VteTerminalPrivate::SPA(vte::parser::Sequence const& seq)
{
        /*
         * SPA - start-of-protected-area
         *
         * TODO: What is this?
         */
}

void
VteTerminalPrivate::SS2(vte::parser::Sequence const& seq)
{
        /*
         * SS2 - single-shift-2
         * Temporarily map G2 into GL for the next graphics character.
         */
#if 0
        screen->state.glt = &screen->g2;
#endif
}

void
VteTerminalPrivate::SS3(vte::parser::Sequence const& seq)
{
        /*
         * SS3 - single-shift-3
         * Temporarily map G3 into GL for the next graphics character
         */
#if 0
        screen->state.glt = &screen->g3;
#endif
}

void
VteTerminalPrivate::ST(vte::parser::Sequence const& seq)
{
        /*
         * ST - string-terminator
         * The string-terminator is usually part of control-sequences and
         * handled by the parser. In all other situations it is silently
         * ignored.
         */
}

void
VteTerminalPrivate::SU(vte::parser::Sequence const& seq)
{
        /*
         * SU - scroll-up
         * XXX
         *
         * Defaults:
         *   args[0]: 1
         *
         * References: EMCA-48 § 8.3.147
         */
#if 0
        unsigned int num = 1;

        if (seq->args[0] > 0)
                num = seq->args[0];

        vte_page_scroll_up(screen->page,
                              num,
                              &screen->state.attr,
                              screen->age,
                              screen->history);
#endif

        auto value = std::max(seq.collect1(0, 1), int(1));
        scroll_text(-value);
}

void
VteTerminalPrivate::SUB(vte::parser::Sequence const& seq)
{
        /*
         * SUB - substitute
         * Cancel the current control-sequence and print a replacement
         * character. Our parser already handles this so all we have to do is
         * print the replacement character.
         */
#if 0
        static const struct vte_seq rep = {
                .type = VTE_SEQ_GRAPHIC,
                .command = VTE_CMD_GRAPHIC,
                .terminator = 0xfffd,
        };

        return screen_GRAPHIC(screen, &rep);
#endif
}

void
VteTerminalPrivate::TAC(vte::parser::Sequence const& seq)
{
        /*
         * TAC - tabulation aligned centre
         *
         * Defaults:
         *   args[0]: no default
         *
         * References: ECMA-48 § 8.3.151
         */
}

void
VteTerminalPrivate::TALE(vte::parser::Sequence const& seq)
{
        /*
         * TALE - tabulation aligned leading edge
         *
         * Defaults:
         *   args[0]: no default
         *
         * References: ECMA-48 § 8.3.152
         */
}

void
VteTerminalPrivate::TATE(vte::parser::Sequence const& seq)
{
        /*
         * TATE - tabulation aligned trailing edge
         *
         * Defaults:
         *   args[0]: no default
         *
         * References: ECMA-48 § 8.3.153
         */
}

void
VteTerminalPrivate::TBC(vte::parser::Sequence const& seq)
{
        /*
         * TBC - tab-clear
         * Clears tab stops.
         *
         * Arguments:
         *   args[0]: mode
         *
         * Defaults:
         *   args[0]: 0
         *
         * References: ECMA-48 § 8.3.154
         */

        auto const param = seq.collect1(0);
        switch (param) {
        case -1:
        case 0:
                /* Clear character tabstop at the current presentation position */
                m_tabstops.unset(m_screen->cursor.col);
                break;
        case 1:
                /* Clear line tabstop at the current line */
                break;
        case 2:
                /* Clear all character tabstops in the current line */
                /* NOTE: vttest issues this but claims it's a 'no-op' */
                m_tabstops.clear();
                break;
        case 3:
                /* Clear all character tabstops */
                m_tabstops.clear();
                break;
        case 4:
                /* Clear all line tabstops */
                break;
        case 5:
                /* Clear all (character and line) tabstops */
                m_tabstops.clear();
                break;
        default:
                break;
	}
}

void
VteTerminalPrivate::TCC(vte::parser::Sequence const& seq)
{
        /*
         * TCC - tabulation centred on character
         *
         * Defaults:
         *   args[0]: no default
         *   args[1]: 32 (SPACE)
         *
         * References: ECMA-48 § 8.3.155
         */
}

void
VteTerminalPrivate::TSR(vte::parser::Sequence const& seq)
{
        /*
         * TSR - tabulation stop remove
         * This clears a tab stop at position @arg[0] in the active line (presentation),
         * and on any lines below it.
         *
         * Defaults:
         *   args[0]: no default
         *
         * References: ECMA-48 § 8.3.156
         */

        auto const pos = seq.collect1(0);
        if (pos < 0 || pos >= m_column_count)
                return;

        m_tabstops.unset(pos);
}

void
VteTerminalPrivate::VPA(vte::parser::Sequence const& seq)
{
        /*
         * VPA - vertical-line-position-absolute
         * XXX
         *
         * Defaults:
         *   args[0]: 1
         */
#if 0
        unsigned int pos = 1;

        if (seq->args[0] > 0)
                pos = seq->args[0];

        screen_cursor_clear_wrap(screen);
        screen_cursor_set_rel(screen, screen->state.cursor_x, pos - 1);
#endif

        // FIXMEchpe shouldn't we ensure_cursor_is_onscreen AFTER setting the new cursor row?
        ensure_cursor_is_onscreen();

        auto value = seq.collect1(0, 1, 1, m_row_count);
        set_cursor_row1(value);
}

void
VteTerminalPrivate::VPR(vte::parser::Sequence const& seq)
{
        /*
         * VPR - vertical-line-position-relative
         * XXX
         *
         * Defaults:
         *   args[0]: 1
         */
#if 0
        unsigned int num = 1;

        if (seq->args[0] > 0)
                num = seq->args[0];

        screen_cursor_clear_wrap(screen);
        screen_cursor_down(screen, num, false);
#endif
}

void
VteTerminalPrivate::VT(vte::parser::Sequence const& seq)
{
        /*
         * VT - vertical-tab
         * This causes a vertical jump by one line. Terminals treat it exactly
         * the same as LF.
         */

        LF(seq);
}

void
VteTerminalPrivate::XTERM_CLLHP(vte::parser::Sequence const& seq)
{
        /*
         * XTERM_CLLHP - xterm-cursor-lower-left-hp-bugfix
         * Move the cursor to the lower-left corner of the page. This is an HP
         * bugfix by xterm.
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::XTERM_IHMT(vte::parser::Sequence const& seq)
{
        /*
         * XTERM_IHMT - xterm-initiate-highlight-mouse-tracking
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::XTERM_MLHP(vte::parser::Sequence const& seq)
{
        /*
         * XTERM_MLHP - xterm-memory-lock-hp-bugfix
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::XTERM_MUHP(vte::parser::Sequence const& seq)
{
        /*
         * XTERM_MUHP - xterm-memory-unlock-hp-bugfix
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::XTERM_RPM(vte::parser::Sequence const& seq)
{
        /*
         * XTERM_RPM - xterm-restore-private-mode
         *
         * Defaults: none
         *
         * References: XTERM
         */

        save_mode_private(seq, false);
}

void
VteTerminalPrivate::XTERM_RRV(vte::parser::Sequence const& seq)
{
        /*
         * XTERM_RRV - xterm-reset-resource-value
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::XTERM_RTM(vte::parser::Sequence const& seq)
{
        /*
         * XTERM_RTM - xterm-reset-title-mode
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::XTERM_SGFX(vte::parser::Sequence const& seq)
{
        /*
         * XTERM_SGFX - xterm-sixel-graphics
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::XTERM_SPM(vte::parser::Sequence const& seq)
{
        /*
         * XTERM_SPM - xterm-set-private-mode
         *
         * Defaults: none
         *
         * References: XTERM
         */

        save_mode_private(seq, true);
}

void
VteTerminalPrivate::XTERM_SRV(vte::parser::Sequence const& seq)
{
        /*
         * XTERM_SRV - xterm-set-resource-value
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::XTERM_STM(vte::parser::Sequence const& seq)
{
        /*
         * XTERM_STM - xterm-set-title-mode
         *
         * Probably not worth implementing.
         */
}

void
VteTerminalPrivate::XTERM_WM(vte::parser::Sequence const& seq)
{
        /*
         * XTERM_WM - xterm-window-management
         *
         * Window manipulation control sequences.  Most of these are considered
         * bad ideas, but they're implemented as signals which the application
         * is free to ignore, so they're harmless.  Handle at most one action,
         * see bug 741402.
         *
         * No parameter default values.
         *
         * References: XTERM
         *             VT525
         */

        #if 0
	char buf[128];
        #endif

        int param = seq.collect1(0);
        switch (param) {
        case -1:
        case 0:
                break;

        case VTE_XTERM_WM_RESTORE_WINDOW:
                _vte_debug_print(VTE_DEBUG_EMULATION, "Deiconifying window.\n");
                emit_deiconify_window();
                break;

        case VTE_XTERM_WM_MINIMIZE_WINDOW:
                _vte_debug_print(VTE_DEBUG_EMULATION, "Iconifying window.\n");
                emit_iconify_window();
                break;

        case VTE_XTERM_WM_SET_WINDOW_POSITION: {
                int pos_x = seq.collect1(1, 0);
                int pos_y = seq.collect1(2, 0);

                _vte_debug_print(VTE_DEBUG_EMULATION,
                                 "Moving window to %d,%d.\n", pos_x, pos_y);
                emit_move_window(pos_x, pos_y);
                break;
        }

        case VTE_XTERM_WM_SET_WINDOW_SIZE_PIXELS: {
                int width, height;
                seq.collect(1, {&height, &width});

                if (width != -1 && height != -1) {
                        _vte_debug_print(VTE_DEBUG_EMULATION,
                                         "Resizing window to %dx%d pixels, grid size %dx%d.\n",
                                         width, height,
                                         width / int(m_cell_height), height / int(m_cell_width));
                        emit_resize_window(width / int(m_cell_height), height / int(m_cell_width));
                }
                break;
        }

        case VTE_XTERM_WM_RAISE_WINDOW:
                _vte_debug_print(VTE_DEBUG_EMULATION, "Raising window.\n");
                emit_raise_window();
                break;

        case VTE_XTERM_WM_LOWER_WINDOW:
                _vte_debug_print(VTE_DEBUG_EMULATION, "Lowering window.\n");
                emit_lower_window();
                break;

        case VTE_XTERM_WM_REFRESH_WINDOW:
                _vte_debug_print(VTE_DEBUG_EMULATION, "Refreshing window.\n");
                invalidate_all();
                emit_refresh_window();
                break;

        case VTE_XTERM_WM_SET_WINDOW_SIZE_CELLS: {
                int width, height;
                seq.collect(1, {&height, &width});

                if (width != -1 && height != -1) {
                        _vte_debug_print(VTE_DEBUG_EMULATION,
                                         "Resizing window to %d columns, %d rows.\n",
                                         width, height);
                        emit_resize_window(width, height);
                }
                break;
        }

        case VTE_XTERM_WM_MAXIMIZE_WINDOW:
                switch (seq.collect1(1)) {
                case -1: /* default */
                case 0:
                        /* Restore */
                        _vte_debug_print(VTE_DEBUG_EMULATION, "Restoring window.\n");
                        emit_restore_window();
                        break;
                case 1:
                        /* Maximise */
                        _vte_debug_print(VTE_DEBUG_EMULATION, "Maximizing window.\n");
                        emit_maximize_window();
                        break;
                case 2:
                        /* Maximise Vertically */
                        break;
                case 3:
                        /* Maximise Horizontally */
                        break;
                default:
                        break;
                }
                break;

        case VTE_XTERM_WM_FULLSCREEN_WINDOW:
                break;

        case VTE_XTERM_WM_GET_WINDOW_STATE:
                /* If we're unmapped, then we're iconified. */
                _vte_debug_print(VTE_DEBUG_EMULATION,
                                 "Reporting window state %siconified.\n",
                                 gtk_widget_get_mapped(m_widget) ? "non-" : "");

                reply(seq, VTE_REPLY_XTERM_WM,
                      {gtk_widget_get_mapped(m_widget) ? 1 : 2});
                break;

        case VTE_XTERM_WM_GET_WINDOW_POSITION: {
                /* Send window location, in pixels. */
                /* FIXME: this is not supported on wayland; just hardwire
                 * it to a fixed return always.
                 */
                int x0, y0;
                gdk_window_get_origin(gtk_widget_get_window(m_widget), &x0, &y0);
                _vte_debug_print(VTE_DEBUG_EMULATION,
                                 "Reporting window location (%d,%d).\n", x0, y0);

                reply(seq, VTE_REPLY_XTERM_WM,
                      {3, x0 + m_padding.left, y0 + m_padding.top});
                break;
        }

        case VTE_XTERM_WM_GET_WINDOW_SIZE_PIXELS: {
                /* Send window size, in pixels. */
                int width = m_row_count * m_cell_height;
                int height = m_column_count * m_cell_width;
                _vte_debug_print(VTE_DEBUG_EMULATION,
                                 "Reporting window size (%dx%d)\n",
                                 width, height);

                reply(seq, VTE_REPLY_XTERM_WM, {4, height, width});
                break;
        }

        case VTE_XTERM_WM_GET_WINDOW_SIZE_CELLS:
                /* Send widget size, in cells. */
                _vte_debug_print(VTE_DEBUG_EMULATION,
                                 "Reporting widget size %ldx%ld\n",
                                 m_row_count, m_column_count);

                reply(seq, VTE_REPLY_XTERM_WM,
                      {8, (int)m_row_count, (int)m_column_count});
                break;

        case VTE_XTERM_WM_GET_SCREEN_SIZE_CELLS: {
                /* FIMXE: this should really report the monitor's workarea,
                 * or even just a fixed value.
                 */
                auto gdkscreen = gtk_widget_get_screen(m_widget);
                int height = gdk_screen_get_height(gdkscreen);
                int width = gdk_screen_get_width(gdkscreen);
                _vte_debug_print(VTE_DEBUG_EMULATION,
                                 "Reporting screen size as %dx%d cells.\n",
                                 height / int(m_cell_height), width / int(m_cell_width));

                reply(seq, VTE_REPLY_XTERM_WM,
                      {9, height / int(m_cell_height), width / int(m_cell_width)});
                break;
        }

        case VTE_XTERM_WM_GET_ICON_TITLE:
                /* Report a static icon title, since the real
                 * icon title should NEVER be reported, as it
                 * creates a security vulnerability.  See
                 * http://marc.info/?l=bugtraq&m=104612710031920&w=2
                 * and CVE-2003-0070.
                 */
                _vte_debug_print(VTE_DEBUG_EMULATION,
                                 "Reporting empty icon title.\n");

                send(seq, vte::parser::u8SequenceBuilder{VTE_SEQ_OSC, "L"s});
                break;

        case VTE_XTERM_WM_GET_WINDOW_TITLE:
                /* Report a static window title, since the real
                 * window title should NEVER be reported, as it
                 * creates a security vulnerability.  See
                 * http://marc.info/?l=bugtraq&m=104612710031920&w=2
                 * and CVE-2003-0070.
                 */
                _vte_debug_print(VTE_DEBUG_EMULATION,
                                 "Reporting empty window title.\n");

                send(seq, vte::parser::u8SequenceBuilder{VTE_SEQ_OSC, "l"s});
                break;

        case VTE_XTERM_WM_TITLE_STACK_PUSH:
                switch (seq.collect1(1)) {
                case -1:
                case VTE_OSC_XTERM_SET_WINDOW_AND_ICON_TITLE:
                case VTE_OSC_XTERM_SET_WINDOW_TITLE:
                        if (m_window_title_stack.size() >= VTE_WINDOW_TITLE_STACK_MAX_DEPTH) {
                                /* Drop the bottommost item */
                                m_window_title_stack.erase(m_window_title_stack.cbegin());
                        }

                        if (m_window_title_changed)
                                m_window_title_stack.emplace(m_window_title_stack.cend(),
                                                             m_window_title_pending);
                        else
                                m_window_title_stack.emplace(m_window_title_stack.cend(),
                                                             m_window_title);

                        g_assert_cmpuint(m_window_title_stack.size(), <=, VTE_WINDOW_TITLE_STACK_MAX_DEPTH);
                        break;

                case VTE_OSC_XTERM_SET_ICON_TITLE:
                default:
                        break;
                }
                break;

        case VTE_XTERM_WM_TITLE_STACK_POP:
                switch (seq.collect1(1)) {
                case -1:
                case VTE_OSC_XTERM_SET_WINDOW_AND_ICON_TITLE:
                case VTE_OSC_XTERM_SET_WINDOW_TITLE:
                        if (m_window_title_stack.empty())
                                break;

                        m_window_title_changed = true;
                        m_window_title_pending.swap(m_window_title_stack.back());
                        m_window_title_stack.pop_back();
                        break;

                case VTE_OSC_XTERM_SET_ICON_TITLE:
                default:
                        break;
                }
                break;

        default:
                /* DECSLPP.
                 *
                 * VTxxx variously supported 24, 25, 36, 41, 42, 48, 52, 53, 72, or 144 rows,
                 * but we support any value >= 24.
                 */
                if (param >= 24) {
                        _vte_debug_print(VTE_DEBUG_EMULATION,
                                         "Resizing to %d rows.\n",
                                         param);
                        /* Resize to the specified number of rows. */
                        emit_resize_window(m_column_count, param);
                }
                break;
        }
}
