/*
 * Copyright (C) 2000-2002 Red Hat, Inc.
 *
 * This is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Library General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ident "$Id$"
#include "../config.h"
#include <sys/types.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>

#ifndef TERMCAP_MAYBE_STATIC
#define TERMCAP_MAYBE_STATIC
#include "termcap.h"
#endif

struct vte_termcap {
	char *comment;
	struct vte_termcap_entry {
		char *comment;
		char *string;
		ssize_t length;
		struct vte_termcap_entry *next;
	} *entries;
	struct vte_termcap_alias {
		char *name;
		struct vte_termcap_entry *entry;
		struct vte_termcap_alias *next;
	} *names;
	GTree *nametree;
};

static char *
nextline(FILE *fp, ssize_t *outlen)
{
	char buf[LINE_MAX];
	ssize_t len = 0;
	char *ret = NULL;
	ssize_t retlen = 0;
	char *tmp = NULL;

	if (!feof(fp)) do {
		if (fgets(buf, sizeof(buf), fp) != buf) {
			break;
		}
		len = strlen(buf);
		tmp = g_malloc(retlen + len + 1);
		if (retlen > 0) {
			memcpy(tmp, ret, retlen);
		}
		memcpy(tmp + retlen, buf, len + 1);
		if (ret != NULL) {
			g_free(ret);
		}
		retlen += len;
		ret = tmp;
		ret[retlen] = '\0';
	} while ((len > 0) && (buf[retlen - 1] != '\n') && !feof(fp));

	if ((ret != NULL) && (retlen > 0) && (ret[retlen - 1] == '\n')) {
		retlen--;
		ret[retlen] = '\0';
	}

	if ((ret != NULL) && (retlen > 0) && (ret[retlen - 1] == '\r')) {
		retlen--;
		ret[retlen] = '\0';
	}

	*outlen = retlen;
	return ret;
}

static char *
nextline_with_continuation(FILE *fp)
{
	char *ret = NULL;
	ssize_t rlen = 0, slen = 0;
	char *s, *tmp;
	gboolean continuation = FALSE;
	do {
		s = nextline(fp, &slen);
		if (s == NULL) {
			break;
		}
		tmp = g_malloc(slen + rlen + 1);
		if (rlen > 0) {
			memcpy(tmp, ret, rlen);
		}
		memcpy(tmp + rlen, s, slen + 1);
		if (ret != NULL) {
			g_free(ret);
		}
		g_free(s);
		ret = tmp;
		rlen += slen;
		if ((rlen > 0) && (ret[rlen - 1] == '\\')) {
			ret[rlen - 1] = '\0';
			rlen--;
			continuation = TRUE;
		} else {
			continuation = FALSE;
		}
	} while ((rlen == 0) || continuation);
	return ret;
}

static void
vte_termcap_add_aliases(struct vte_termcap *termcap,
			struct vte_termcap_entry *entry,
			const char *aliases)
{
	ssize_t l;
	struct vte_termcap_alias *alias = NULL;
	const char *p;

	for (p = aliases, l = 0; p != NULL; l++) {
		if (aliases[l] == '\\') {
			l++;
		} else
		if ((aliases[l] == '|') ||
		   (aliases[l] == ':') ||
		   (aliases[l] == '\0')) {
			alias = g_malloc(sizeof(struct vte_termcap_alias));
			if (alias) {
				memset(alias, 0, sizeof(*alias));
				alias->name = g_strndup(p, &aliases[l] - p);
				alias->entry = entry;
				alias->next = termcap->names;
				termcap->names = alias;
				if (aliases[l] == '\0') {
					p = NULL;
				} else {
					p = &aliases[l + 1];
				}
				g_tree_insert(termcap->nametree,
					      GINT_TO_POINTER(g_quark_from_string(alias->name)),
					      alias);
			}
			l++;
		}
	}
}

static void
vte_termcap_add_entry(struct vte_termcap *termcap, const char *s, ssize_t length,
		      char *comment)
{
	struct vte_termcap_entry *entry = NULL;
	char *p = NULL;
	ssize_t l;

	entry = g_malloc(sizeof(struct vte_termcap_entry));
	if (entry != NULL) {
		memset(entry, 0, sizeof(struct vte_termcap_entry));
		entry->string = g_malloc(length + 1);
		if (length > 0) {
			memcpy(entry->string, s, length);
		}
		entry->string[length] = '\0';
		entry->length = length;
		entry->comment = comment;
		entry->next = termcap->entries;
		termcap->entries = entry;
		for (l = 0; l < length; l++) {
			if (s[l] == '\\') {
				l++;
				continue;
			}
			if (s[l] == ':') {
				break;
			}
		}
		if (l <= length) {
			p = g_malloc(l + 1);
			if (p) {
				strncpy(p, s, l);
				p[l] = '\0';
				vte_termcap_add_aliases(termcap, entry, p);
				g_free(p);
			}
		}
	}
}

static void
vte_termcap_strip(const char *termcap, char **stripped, ssize_t *len)
{
	char *ret;
	ssize_t i, o, length;
	length = strlen(termcap);

	ret = g_malloc(length + 2);
	for (i = o = 0; i < length; i++) {
		ret[o++] = termcap[i];
		if (termcap[i] == '\\') {
			char *p;
			switch(termcap[i + 1]) {
				case '\n':
					while ((termcap[i + 1] == ' ') ||
					       (termcap[i + 1] == '\t')) {
						i++;
					}
					continue;
				case 'E':
					i++;
					ret[o - 1] = 27;
					continue;
				case 'n':
					i++;
					ret[o - 1] = 10;
					continue;
				case 'r':
					i++;
					ret[o - 1] = 13;
					continue;
				case 't':
					i++;
					ret[o - 1] = 8;
					continue;
				case 'b':
					i++;
					ret[o - 1] = 9;
					continue;
				case 'f':
					i++;
					ret[o - 1] = 12;
					continue;
				case '0':
				case '1':
					i++;
					ret[o - 1] = strtol(termcap + i, &p, 8);
					p--;
					i = p - termcap;
					continue;
			}
		} else
		if (termcap[i] == '^') {
			switch(termcap[i + 1]) {
				case 'A':
				case 'B':
				case 'C':
				case 'D':
				case 'E':
				case 'F':
				case 'G':
				case 'H':
				case 'I':
				case 'J':
				case 'K':
				case 'L':
				case 'M':
				case 'N':
				case 'O':
				case 'P':
				case 'Q':
				case 'R':
				case 'S':
				case 'T':
				case 'U':
				case 'V':
				case 'W':
				case 'X':
				case 'Y':
				case 'Z':
					i++;
					ret[o - 1] = termcap[i] - ('A' - 1);
					continue;
				default:
					break;
			}
		} else {
			if (termcap[i] == ':') {
				while ((termcap[i + 1] == ' ') ||
				       (termcap[i + 1] == '\t')) {
					i++;
				}
				continue;
			}
		}
	}
	ret[o] = ':';
	o++;
	ret[o] = '\0';
	*stripped = ret;
	*len = o;
}

static gint
vte_direct_compare(gconstpointer a, gconstpointer b)
{
	return GPOINTER_TO_INT(a) - GPOINTER_TO_INT(b);
}

TERMCAP_MAYBE_STATIC struct vte_termcap *
vte_termcap_new(const char *filename)
{
	FILE *fp;
	char *s, *stripped, *comment = NULL;
	struct vte_termcap *ret = NULL;
	fp = fopen(filename, "r");
	if (fp != NULL) {
		while ((s = nextline_with_continuation(fp)) != NULL) {
			ssize_t slen;
			if ((s[0] != '#') && (isprint(s[0]))) {
				if (ret == NULL) {
					ret = g_malloc(sizeof(struct vte_termcap));
					if (ret == NULL) {
						return NULL;
					}
					memset(ret, 0, sizeof(struct vte_termcap));
					ret->nametree = g_tree_new(vte_direct_compare);
				}
				stripped = NULL;
				vte_termcap_strip(s, &stripped, &slen);
				if (stripped) {
					vte_termcap_add_entry(ret, stripped,
							      slen, comment);
					comment = NULL;
					g_free(stripped);
				}
			} else {
				slen = strlen(s);
				if (comment == NULL) {
					comment = g_malloc(slen + 2);
					memcpy(comment, s, slen);
					comment[slen] = '\n';
					comment[slen + 1] = '\0';
				} else {
					char *tmp;
					ssize_t clen;
					clen = strlen(comment);
					tmp = g_malloc(slen + clen + 2);
					if (tmp == NULL) {
						return NULL;
					}
					memcpy(tmp, comment, clen);
					memcpy(tmp + clen, s, slen);
					tmp[clen + slen] = '\n';
					tmp[clen + slen + 1] = '\0';
					g_free(comment);
					comment = tmp;
				}
			}
			g_free(s);
		}
		ret->comment = comment;
		fclose(fp);
	}
	return ret;
}

TERMCAP_MAYBE_STATIC void
vte_termcap_free(struct vte_termcap *termcap)
{
	struct vte_termcap_entry *entry, *nextentry;
	struct vte_termcap_alias *alias, *nextalias;
	for (entry = termcap->entries; entry != NULL; entry = nextentry) {
		nextentry = entry->next;
		g_free(entry->comment);
		g_free(entry->string);
		g_free(entry);
	}
	for (alias = termcap->names; alias != NULL; alias = nextalias) {
		nextalias = alias->next;
		g_free(alias->name);
		g_free(alias);
	}
	g_tree_destroy(termcap->nametree);
	g_free(termcap->comment);
	g_free(termcap);
}

static const char *
vte_termcap_find_l(struct vte_termcap *termcap, const char *tname, ssize_t len,
		   const char *cap)
{
	const char *ret;
	struct vte_termcap_alias *alias;
	char ttname[len + 1];
	ssize_t clen;

	g_return_val_if_fail(termcap != NULL, "");
	g_return_val_if_fail(tname != NULL, "");
	g_return_val_if_fail(len > 0, "");
	g_return_val_if_fail(cap != NULL, "");
	g_return_val_if_fail(strlen(cap) > 0, "");

	/* Find the entry by this name. */
	memcpy(ttname, tname, len);
	ttname[len] = '\0';
	alias = g_tree_lookup(termcap->nametree,
			      GINT_TO_POINTER(g_quark_from_string(ttname)));

	/* If we found the entry, poke around in it. */
	if (alias != NULL) {
		char *str = alias->entry->string;
		const char *nextcap = "tc";
		ssize_t len = alias->entry->length;

		clen = strlen(cap);
		ret = str;

		/* Search for the capability in this entry. */
		for (ret = str - 1;
		     ret != NULL;
		     ret = memchr(ret, ':', str + len - ret - clen)) {
			/* We've hit the first separator, or are before the
			 * very first part of the entry, so hit the next
			 * capability. */
			ret++;
			/* If the end of the entry's name isn't the end of the
			 * string, and it isn't a boolean/string/numeric, or
			 * its name is wrong, keep looking. */
			if (((ret[clen] != '\0') &&
			     (ret[clen] != ':') &&
			     (ret[clen] != '=') &&
			     (ret[clen] != '#')) ||
			    (memcmp(ret, cap, clen) != 0)) {
				continue;
			}
			/* Found it. */
			return ret;
		}

		/* Now find the "tc=" entries, and scan those entries. */
		clen = strlen(nextcap);
		ret = str - 1;

		while (ret != NULL) {
			for (;
			     ret != NULL;
			     ret = memchr(ret, ':', str + len - ret - clen)) {
				ret++;
				if (((ret[clen] != '\0') &&
				     (ret[clen] != ':') &&
				     (ret[clen] != '=') &&
				     (ret[clen] != '#')) ||
				    (memcmp(ret, nextcap, clen) != 0)) {
					continue;
				}
				break;
			}

			if (ret != NULL) {
				const char *t;
				char *end;
				end = strchr(ret + clen + 1, ':');
				if (end != NULL) {
					t = vte_termcap_find_l(termcap,
							       ret + clen + 1,
							       end -
							       (ret + clen + 1),
							       cap);
				} else {
					t = vte_termcap_find_l(termcap,
							       ret + clen + 1,
							       strlen(ret +
								      clen + 1),
							       cap);
				}
				if ((t != NULL) && (t[0] != '\0')) {
					return t;
				}
				ret++;
			}
		}
	}
	return "";
}

static const char *
vte_termcap_find(struct vte_termcap *termcap,
		 const char *tname, const char *cap)
{
	g_return_val_if_fail(termcap != NULL, "");
	return vte_termcap_find_l(termcap, tname, strlen(tname), cap);
}

TERMCAP_MAYBE_STATIC gboolean
vte_termcap_find_boolean(struct vte_termcap *termcap, const char *tname,
			 const char *cap)
{
	const char *val;
	g_return_val_if_fail(termcap != NULL, FALSE);
	val = vte_termcap_find(termcap, tname, cap);
	if ((val != NULL) && (val[0] != '\0')) {
		return TRUE;
	}
	return FALSE;
}

TERMCAP_MAYBE_STATIC long
vte_termcap_find_numeric(struct vte_termcap *termcap, const char *tname,
			 const char *cap)
{
	const char *val;
	char *p;
	ssize_t l;
	long ret;
	g_return_val_if_fail(termcap != NULL, 0);
	val = vte_termcap_find(termcap, tname, cap);
	if ((val != NULL) && (val[0] != '\0')) {
		l = strlen(cap);
		ret = strtol(val + l + 1, &p, 0);
		if ((p != NULL) && ((*p == '\0') || (*p == ':'))) {
			return ret;
		}
	}
	return 0;
}

TERMCAP_MAYBE_STATIC char *
vte_termcap_find_string(struct vte_termcap *termcap, const char *tname,
			const char *cap)
{
	const char *val, *p;
	ssize_t l;
	val = vte_termcap_find(termcap, tname, cap);
	if ((val != NULL) && (val[0] != '\0')) {
		l = strlen(cap);
		val += (l + 1);
		p = strchr(val, ':');
		if (p) {
			return g_strndup(val, p - val);
		} else {
			return g_strdup(val);
		}
	}
	return g_strdup("");
}

TERMCAP_MAYBE_STATIC char *
vte_termcap_find_string_length(struct vte_termcap *termcap, const char *tname,
			       const char *cap, ssize_t *length)
{
	const char *val, *p;
	char *ret;
	ssize_t l;
	val = vte_termcap_find(termcap, tname, cap);
	if ((val != NULL) && (val[0] != '\0')) {
		l = strlen(cap);
		val += (l + 1);
		p = val;
		while (*p != ':') p++;
		*length = l = p - val;
		ret = g_malloc(l + 1);
		if (l > 0) {
			memcpy(ret, val, l);
		}
		ret[l] = '\0';
		return ret;
	}
	return g_strdup("");
}

TERMCAP_MAYBE_STATIC const char *
vte_termcap_comment(struct vte_termcap *termcap, const char *tname)
{
	struct vte_termcap_alias *alias;
	ssize_t len;
	if ((tname == NULL) || (tname[0] == '\0')) {
		return termcap->comment;
	}
	len = strlen(tname);
	for (alias = termcap->names; alias != NULL; alias = alias->next) {
		if (strncmp(tname, alias->name, len) == 0) {
			if (alias->name[len] == '\0') {
				break;
			}
		}
	}
	if (alias && (alias->entry != NULL)) {
		return alias->entry->comment;
	}
	return NULL;
}

/* FIXME: should escape characters we've previously decoded. */
TERMCAP_MAYBE_STATIC char *
vte_termcap_generate(struct vte_termcap *termcap)
{
	ssize_t size;
	char *ret = NULL;
	struct vte_termcap_entry *entry;
	size = strlen(termcap->comment ?: "");
	for (entry = termcap->entries; entry != NULL; entry = entry->next) {
		size += strlen(entry->comment ?: "");
		size += (strlen(entry->string ?: "") + 1);
	}
	ret = g_malloc(size + 1);
	if (ret == NULL) {
		return NULL;
	}
	memset(ret, '\0', size);
	size = 0;
	for (entry = termcap->entries; entry != NULL; entry = entry->next) {
		if (entry->comment) {
			memcpy(ret + size, entry->comment,
			       strlen(entry->comment));
			size += strlen(entry->comment);
		}
		if (entry->string) {
			memcpy(ret + size, entry->string,
			       strlen(entry->string));
			size += strlen(entry->string);
			ret[size] = '\n';
			size++;
		}
	}
	if (termcap->comment) {
		memcpy(ret + size, termcap->comment,
		       strlen(termcap->comment));
		size += strlen(termcap->comment);
	}
	return ret;
}

#ifdef TERMCAP_MAIN
int
main(int argc, char **argv)
{
	const char *tc = (argc > 1) ? argv[1] : "linux";
	const char *cap = (argc > 2) ? argv[2] : "so";
	char *value;
	struct vte_termcap *termcap = vte_termcap_new("/etc/termcap");
	value = vte_termcap_find_string(termcap, tc, cap);
	printf("%s\n", value);
	g_free(value);
	vte_termcap_free(termcap);
	return 0;
}
#endif
