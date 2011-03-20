/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2010, 2011 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#ifndef DATA_IDENTIFIER_H
#define DATA_IDENTIFIER_H 1

#include <ctype.h>
#include <stdbool.h>
#include <unitypes.h>
#include "libpspp/str.h"

#define TOKEN_TYPES                                                     \
    TOKEN_TYPE(ID)                         /* Identifier. */            \
    TOKEN_TYPE(POS_NUM)                    /* Positive number. */       \
    TOKEN_TYPE(NEG_NUM)                    /* Negative number. */       \
    TOKEN_TYPE(STRING)                     /* Quoted string. */         \
    TOKEN_TYPE(STOP)                       /* End of input. */          \
                                                                        \
    TOKEN_TYPE(ENDCMD)                     /* . */                      \
    TOKEN_TYPE(PLUS)                       /* + */                      \
    TOKEN_TYPE(DASH)                       /* - */                      \
    TOKEN_TYPE(ASTERISK)                   /* * */                      \
    TOKEN_TYPE(SLASH)                      /* / */                      \
    TOKEN_TYPE(EQUALS)                     /* = */                      \
    TOKEN_TYPE(LPAREN)                     /* ( */                      \
    TOKEN_TYPE(RPAREN)                     /* ) */                      \
    TOKEN_TYPE(LBRACK)                     /* [ */                      \
    TOKEN_TYPE(RBRACK)                     /* ] */                      \
    TOKEN_TYPE(COMMA)                      /* , */                      \
                                                                        \
    TOKEN_TYPE(AND)                        /* AND */                    \
    TOKEN_TYPE(OR)                         /* OR */                     \
    TOKEN_TYPE(NOT)                        /* NOT */                    \
                                                                        \
    TOKEN_TYPE(EQ)                         /* EQ */                     \
    TOKEN_TYPE(GE)                         /* GE or >= */               \
    TOKEN_TYPE(GT)                         /* GT or > */                \
    TOKEN_TYPE(LE)                         /* LE or <= */               \
    TOKEN_TYPE(LT)                         /* LT or < */                \
    TOKEN_TYPE(NE)                         /* NE or ~= */               \
                                                                        \
    TOKEN_TYPE(ALL)                        /* ALL */                    \
    TOKEN_TYPE(BY)                         /* BY */                     \
    TOKEN_TYPE(TO)                         /* TO */                     \
    TOKEN_TYPE(WITH)                       /* WITH */                   \
                                                                        \
    TOKEN_TYPE(EXP)                        /* ** */

/* Token types. */
enum token_type
  {
#define TOKEN_TYPE(TYPE) T_##TYPE,
    TOKEN_TYPES
    TOKEN_N_TYPES
#undef TOKEN_TYPE
  };

const char *token_type_to_name (enum token_type);
const char *token_type_to_string (enum token_type);

/* Tokens. */
bool lex_is_keyword (enum token_type);

/* Validating identifiers. */
#define ID_MAX_LEN 64          /* Maximum length of identifier, in bytes. */

bool id_is_valid (const char *id, const char *dict_encoding, bool issue_error);
bool id_is_plausible (const char *id, bool issue_error);

/* Recognizing identifiers. */
bool lex_is_id1 (char);
bool lex_is_idn (char);
bool lex_uc_is_id1 (ucs4_t);
bool lex_uc_is_idn (ucs4_t);
bool lex_uc_is_space (ucs4_t);
size_t lex_id_get_length (struct substring);

/* Comparing identifiers. */
bool lex_id_match (struct substring keyword, struct substring token);
bool lex_id_match_n (struct substring keyword, struct substring token,
                     size_t n);
int lex_id_to_token (struct substring);

#endif /* !data/identifier.h */
