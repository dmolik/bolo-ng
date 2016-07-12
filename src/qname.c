#include <bolo.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "debug.h"

#define MAX_PAIRS 64

struct __bolo_qname {
	unsigned int pairs;       /* how many key/value pairs are there?   */
	unsigned int length;      /* how long is the canonical string rep? */
	int wildcard;             /* is this a wildcard pattern match?     */

	char *keys[MAX_PAIRS];    /* keys (pointers into flyweight)        */
	char *values[MAX_PAIRS];  /* values (pointers into flyweight)      */

	char flyweight[];         /* munged copy of original qn string     */
};

/* constants for use in the Parser Finite State Machine
   (refer to docs/qname-fsm.dot for more details)
 */
#define BOLO_PFSM_K1 1
#define BOLO_PFSM_K2 2
#define BOLO_PFSM_V1 3
#define BOLO_PFSM_V2 4
#define BOLO_PFSM_M  5

#include "qname_chars.inc"
#define s_is_character(c)  (TBL_QNAME_CHARACTER[((c) & 0xff)] == 1)
#define s_is_wildcard(c)   ((c) == '*')

#define swap(x,y) do { \
	x = (char *)((uintptr_t)(x) ^ (uintptr_t)(y)); \
	y = (char *)((uintptr_t)(y) ^ (uintptr_t)(x)); \
	x = (char *)((uintptr_t)(x) ^ (uintptr_t)(y)); \
} while (0)

static void
s_sort(int num, char **keys, char **values)
{
	/* dumb insertion sort algorithm */
	int i, j;

	for (i = 1; i < num; i++) {
		j = i;
		while (j > 0 && strcmp(keys[j-1], keys[j]) > 0) {
			swap(keys[j-1], keys[j]);
			swap(values[j-1], values[j]);
		}
	}
}

bolo_qname_t
bolo_qname_parse(const char *string)
{
	bolo_qname_t qn;    /* the qualified name itself (allocated)     */
	const char *p;      /* pointer for iterating over string         */
	char *fill;         /* pointer for filling flyweight             */
	int fsm;            /* parser finite state machine state         */
	int escaped;        /* last token was backslash (1) or not (0)   */

	if (!string) {
		debugf("invalid input string (%p / %s)\n", string, string);
		return INVALID_QNAME;
	}

	qn = calloc(1, sizeof(struct __bolo_qname)
	             + strlen(string) /* flyweight       */
	             + 1);            /* NULL-terminator */
	if (!qn) {
		debugf("alloc(%lu + %lu + %lu) [=%lu] failed\n", sizeof(struct __bolo_qname), strlen(string), 1LU,
				sizeof(struct __bolo_qname) + strlen(string) + 1LU);
		return INVALID_QNAME;
	}

	escaped = 0;
	fsm = BOLO_PFSM_K1;
	fill = qn->flyweight;
	for (p = string; *p; p++) {
		if (*p == '\\') {
			escaped = 1;
			continue;
		}
		if (escaped) {
			switch (fsm) {
			case BOLO_PFSM_K1:
				fsm = BOLO_PFSM_K2;
				qn->keys[qn->pairs] = fill;
			case BOLO_PFSM_K2:
				*fill++ = *p;
				qn->length++;
				break;

			case BOLO_PFSM_V1:
				fsm = BOLO_PFSM_V2;
				qn->values[qn->pairs] = fill;
			case BOLO_PFSM_V2:
				*fill++ = *p;
				qn->length++;
				break;

			default:
				free(qn);
				debugf("invalid FSM state [%d] for escape sequence\n", fsm);
				return INVALID_QNAME;
			}
			escaped = 0;
			continue;
		}

		switch (fsm) {
		case BOLO_PFSM_K1:
			if (s_is_character(*p)) {
				qn->keys[qn->pairs] = fill;
				*fill++ = *p;
				qn->length++;
				fsm = BOLO_PFSM_K2;

			} else if (s_is_wildcard(*p)) {
				qn->keys[qn->pairs] = fill;
				*fill++ = *p;
				qn->length++;
				fsm = BOLO_PFSM_M;

			} else {
				free(qn);
				debugf("invalid token (%c / %#02x) for transition from state K1\n", *p, *p);
				return INVALID_QNAME;
			}
			break;


		case BOLO_PFSM_K2:
			if (*p == '=') {
				*fill++ = '\0';
				qn->length++;
				fsm = BOLO_PFSM_V1;

			} else if (*p == ',') {
				*fill++ = '\0';
				qn->length++;
				qn->values[qn->pairs] = NULL;
				qn->pairs++;
				if (qn->pairs >= MAX_PAIRS) {
					free(qn);
					debugf("exceeded MAX_PAIRS (%d) after key '%s'\n", MAX_PAIRS, qn->keys[qn->pairs-1]);
					return INVALID_QNAME;
				}
				fsm = BOLO_PFSM_K1;

			} else if (s_is_character(*p)) {
				*fill++ = *p;
				qn->length++;

			} else {
				free(qn);
				debugf("invalid token (%c / %#02x) for transition from state K2\n", *p, *p);
				return INVALID_QNAME;
			}
			break;


		case BOLO_PFSM_V1:
			if (s_is_character(*p)) {
				qn->values[qn->pairs] = fill;
				*fill++ = *p;
				qn->length++;
				fsm = BOLO_PFSM_V2;

			} else if (s_is_wildcard(*p)) {
				qn->values[qn->pairs] = fill;
				*fill++ = *p;
				qn->length++;
				fsm = BOLO_PFSM_M;

			} else if (*p == ',') {
				*fill++ = '\0';
				qn->length++;
				qn->pairs++;
				if (qn->pairs >= MAX_PAIRS) {
					free(qn);
					debugf("exceeded MAX_PAIRS (%d) after key '%s'\n", MAX_PAIRS, qn->keys[qn->pairs-1]);
					return INVALID_QNAME;
				}
				fsm = BOLO_PFSM_K1;

			} else {
				free(qn);
				debugf("invalid token (%c / %#02x) for transition from state V1\n", *p, *p);
				return INVALID_QNAME;
			}
			break;


		case BOLO_PFSM_V2:
			if (*p == ',') {
				*fill++ = '\0';
				qn->length++;
				qn->pairs++;
				if (qn->pairs >= MAX_PAIRS) {
					free(qn);
					debugf("exceeded MAX_PAIRS (%d) after key '%s'\n", MAX_PAIRS, qn->keys[qn->pairs-1]);
					return INVALID_QNAME;
				}
				fsm = BOLO_PFSM_K1;

			} else if (s_is_character(*p)) {
				*fill++ = *p;
				qn->length++;

			} else {
				free(qn);
				debugf("invalid token (%c / %#02x) for transition from state V2\n", *p, *p);
				return INVALID_QNAME;
			}
			break;


		case BOLO_PFSM_M:
			if (*p == ',') {
				*fill++ = '\0';
				qn->length++;
				qn->pairs++;
				if (qn->pairs >= MAX_PAIRS) {
					free(qn);
					debugf("exceeded MAX_PAIRS (%d) after key '%s'\n", MAX_PAIRS, qn->keys[qn->pairs-1]);
					return INVALID_QNAME;
				}
				fsm = BOLO_PFSM_K1;

			} else {
				free(qn);
				debugf("invalid token (%c / %#02x) for transition from state M\n", *p, *p);
				return INVALID_QNAME;
			}
			break;


		defaut:
			free(qn);
			debugf("invalid FSM state [%d]\n", fsm);
			return INVALID_QNAME;
		}
	}

	/* EOF; check states that can legitimately lead to DONE */
	switch (fsm) {
	case BOLO_PFSM_K2:
	case BOLO_PFSM_V1:
	case BOLO_PFSM_V2:
		qn->pairs++;
		break;

	default:
		free(qn);
		debugf("invalid final FSM state [%d]", fsm);
		return INVALID_QNAME;
	}

	/* sort the key/value pairs lexcially by key, to make
	   comparison and stringification easier, later */
	s_sort(qn->pairs, qn->keys, qn->values);
	return qn;
}


void
bolo_qname_free(bolo_qname_t qn)
{
	free(qn);
}


char *
bolo_qname_string(bolo_qname_t qn)
{
	char *string, *fill, *tombstone;
	const char *p;
	int i;

	if (qn == INVALID_QNAME) {
		return strdup("");
	}

	string = calloc(qn->length + 1, sizeof(char));
	if (!string) {
		debugf("alloc(%u + %lu, %lu) [=%lu] failed\n", qn->length, 1LU, sizeof(char),
				(qn->length + 1LU) * sizeof(char));
		return NULL;
	}

	fill = string;
	tombstone = fill + qn->length;
	for (i = 0; i < qn->pairs && i < MAX_PAIRS; i++) {
		p = qn->keys[i];
		while (*p && fill != tombstone)
			*fill++ = *p++;
		if ( (p = qn->values[i]) != NULL) {
			*fill++ = '=';
			while (*p && fill != tombstone)
				*fill++ = *p++;
		}
		if (i + 1 != qn->pairs) {
			*fill++ = ',';
		}
	}
	/* FIXME: trailing wildcard! */

	return string;
}

int
bolo_qname_equal(bolo_qname_t a, bolo_qname_t b)
{
	unsigned int i;
	/* the invalid qualified name is never equivalent to anything */
	if (a == INVALID_QNAME || b == INVALID_QNAME) {
		return 0;
	}

	/* wildcard names only match other wildcard names */
	if (a->wildcard != b->wildcard) {
		return 0;
	}

	/* equivalent names have the same number of key=value pairs */
	if (a->pairs != b->pairs) {
		return 0;
	}

	/* pairs should be lexically ordered, so we can compare
	   them sequentially for key- and value-equality */
	for (i = 0; i < a->pairs; i++) {
		if (!a->keys[i] || !b->keys[i]) {
			return 0; /* it is an error to have NULL keys */
		}
		if (strcmp(a->keys[i], b->keys[i]) != 0) {
			return 0; /* key mismatch */
		}
		if (!!a->values[i] != !!b->values[i]) {
			return 0; /* value not present in both */
		}
		if (strcmp(a->values[i], b->values[i]) != 0) {
			return 0; /* value mismatch */
		}
	}
	return 1;
}

int
bolo_qname_match(bolo_qname_t qn, bolo_qname_t pattern)
{
	unsigned int i, j;
	int found;

	/* the invalid qualified name never matches to anything */
	if (qn == INVALID_QNAME || pattern == INVALID_QNAME) {
		return 0;
	}

	for (i = 0; i < qn->pairs; i++) {
		if (!qn->keys[i]) {
			return 0; /* it is an error to have NULL keys */
		}

		/* see if the key is present in the pattern */
		found = 0;
		for (j = 0; j < pattern->pairs; j++) {
			if (!pattern->keys[j]) {
				return 0; /* it is an error to have NULL keys */
			}
			if (strcmp(qn->keys[i], pattern->keys[j]) == 0) {
				found = 1;
				break;
			}
		}
		if (!found && !pattern->wildcard) {
			return 0; /* pattern doesn't have this key, and isn't wildcard */
		}

		/* FIXME: need a better way of handling partial match (bitmap?) */
		if (pattern->values[j] && strcmp(pattern->values[j], "*") == 0) {
			continue;
		}
		if (!!qn->values[i] != !!pattern->values[i]) {
			return 0; /* value not present in both */
		}
		if (strcmp(qn->values[i], pattern->values[j]) != 0) {
			return 0; /* value mismatch */
		}
	}
	return 1;
}
