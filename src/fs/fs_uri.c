/*
     This file is part of GNUnet.
     Copyright (C) 2003--2014 Christian Grothoff (and other contributing authors)

     GNUnet is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published
     by the Free Software Foundation; either version 3, or (at your
     option) any later version.

     GNUnet is distributed in the hope that it will be useful, but
     WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     General Public License for more details.

     You should have received a copy of the GNU General Public License
     along with GNUnet; see the file COPYING.  If not, write to the
     Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
     Boston, MA 02110-1301, USA.
*/

/**
 * @file fs/fs_uri.c
 * @brief Parses and produces uri strings.
 * @author Igor Wronsky, Christian Grothoff
 *
 * GNUnet URIs are of the general form "gnunet://MODULE/IDENTIFIER".
 * The specific structure of "IDENTIFIER" depends on the module and
 * maybe differenciated into additional subcategories if applicable.
 * This module only deals with fs identifiers (MODULE = "fs").
 * <p>
 *
 * This module only parses URIs for the AFS module.  The FS URIs fall
 * into four categories, "chk", "sks", "ksk" and "loc".  The first three
 * categories were named in analogy (!) to Freenet, but they do NOT
 * work in exactly the same way.  They are very similar from the user's
 * point of view (unique file identifier, subspace, keyword), but the
 * implementation is rather different in pretty much every detail.
 * The concrete URI formats are:
 *
 * <ul><li>
 *
 * First, there are URIs that identify a file.  They have the format
 * "gnunet://fs/chk/HEX1.HEX2.SIZE".  These URIs can be used to
 * download the file.  The description, filename, mime-type and other
 * meta-data is NOT part of the file-URI since a URI uniquely
 * identifies a resource (and the contents of the file would be the
 * same even if it had a different description).
 *
 * </li><li>
 *
 * The second category identifies entries in a namespace.  The format
 * is "gnunet://fs/sks/NAMESPACE/IDENTIFIER" where the namespace
 * should be given in HEX.  Applications may allow using a nickname
 * for the namespace if the nickname is not ambiguous.  The identifier
 * can be either an ASCII sequence or a HEX-encoding.  If the
 * identifier is in ASCII but the format is ambiguous and could denote
 * a HEX-string a "/" is appended to indicate ASCII encoding.
 *
 * </li> <li>
 *
 * The third category identifies ordinary searches.  The format is
 * "gnunet://fs/ksk/KEYWORD[+KEYWORD]*".  Using the "+" syntax
 * it is possible to encode searches with the boolean "AND" operator.
 * "+" is used since it indicates a commutative 'and' operation and
 * is unlikely to be used in a keyword by itself.
 *
 * </li><li>
 *
 * The last category identifies a datum on a specific machine.  The
 * format is "gnunet://fs/loc/HEX1.HEX2.SIZE.PEER.SIG.EXPTIME".  PEER is
 * the BinName of the public key of the peer storing the datum.  The
 * signature (SIG) certifies that this peer has this content.
 * HEX1, HEX2 and SIZE correspond to a 'chk' URI.
 *
 * </li></ul>
 *
 * The encoding for hexadecimal values is defined in the hashing.c
 * module in the gnunetutil library and discussed there.
 * <p>
 */
#include "platform.h"
#include "gnunet_fs_service.h"
#include "gnunet_signatures.h"
#include "fs_api.h"
#include <unitypes.h>
#include <unicase.h>
#include <uniconv.h>
#include <unistr.h>
#include <unistdio.h>



/**
 * Get a unique key from a URI.  This is for putting URIs
 * into HashMaps.  The key may change between FS implementations.
 *
 * @param uri uri to convert to a unique key
 * @param key where to store the unique key
 */
void
GNUNET_FS_uri_to_key (const struct GNUNET_FS_Uri *uri,
		      struct GNUNET_HashCode *key)
{
  switch (uri->type)
  {
  case GNUNET_FS_URI_CHK:
    *key = uri->data.chk.chk.query;
    return;
  case GNUNET_FS_URI_SKS:
    GNUNET_CRYPTO_hash (uri->data.sks.identifier,
                        strlen (uri->data.sks.identifier), key);
    break;
  case GNUNET_FS_URI_KSK:
    if (uri->data.ksk.keywordCount > 0)
      GNUNET_CRYPTO_hash (uri->data.ksk.keywords[0],
                          strlen (uri->data.ksk.keywords[0]), key);
    break;
  case GNUNET_FS_URI_LOC:
    GNUNET_CRYPTO_hash (&uri->data.loc.fi,
                        sizeof (struct FileIdentifier) +
                        sizeof (struct GNUNET_PeerIdentity),
                        key);
    break;
  default:
    memset (key, 0, sizeof (struct GNUNET_HashCode));
    break;
  }
}


/**
 * Convert keyword URI to a human readable format
 * (i.e. the search query that was used in the first place)
 *
 * @param uri ksk uri to convert to a string
 * @return string with the keywords
 */
char *
GNUNET_FS_uri_ksk_to_string_fancy (const struct GNUNET_FS_Uri *uri)
{
  size_t n;
  char *ret;
  unsigned int i;
  const char *keyword;
  char **keywords;
  unsigned int keywordCount;

  if ((NULL == uri) || (GNUNET_FS_URI_KSK != uri->type))
  {
    GNUNET_break (0);
    return NULL;
  }
  keywords = uri->data.ksk.keywords;
  keywordCount = uri->data.ksk.keywordCount;
  n = keywordCount + 1;
  for (i = 0; i < keywordCount; i++)
  {
    keyword = keywords[i];
    n += strlen (keyword) - 1;
    if (NULL != strstr (&keyword[1], " "))
      n += 2;
    if (keyword[0] == '+')
      n++;
  }
  ret = GNUNET_malloc (n);
  strcpy (ret, "");
  for (i = 0; i < keywordCount; i++)
  {
    keyword = keywords[i];
    if (NULL != strstr (&keyword[1], " "))
    {
      strcat (ret, "\"");
      if (keyword[0] == '+')
        strcat (ret, keyword);
      else
        strcat (ret, &keyword[1]);
      strcat (ret, "\"");
    }
    else
    {
      if (keyword[0] == '+')
        strcat (ret, keyword);
      else
        strcat (ret, &keyword[1]);
    }
    strcat (ret, " ");
  }
  return ret;
}


/**
 * Given a keyword with %-encoding (and possibly quotes to protect
 * spaces), return a copy of the keyword without %-encoding and
 * without double-quotes (%22).  Also, add a space at the beginning
 * if there is not a '+'.
 *
 * @param in string with %-encoding
 * @param emsg where to store the parser error message (if any)
 * @return decodded string with leading space (or preserved plus)
 */
static char *
percent_decode_keyword (const char *in,
                        char **emsg)
{
  char *out;
  char *ret;
  unsigned int rpos;
  unsigned int wpos;
  unsigned int hx;

  out = GNUNET_strdup (in);
  rpos = 0;
  wpos = 0;
  while (out[rpos] != '\0')
  {
    if (out[rpos] == '%')
    {
      if (1 != SSCANF (&out[rpos + 1], "%2X", &hx))
      {
        GNUNET_free (out);
        *emsg = GNUNET_strdup (_(/* xgettext:no-c-format */
				 "Malformed KSK URI (`%' must be followed by HEX number)"));
        return NULL;
      }
      rpos += 3;
      if (hx == '"')
        continue;               /* skip double quote */
      out[wpos++] = (char) hx;
    }
    else
    {
      out[wpos++] = out[rpos++];
    }
  }
  out[wpos] = '\0';
  if (out[0] == '+')
  {
    ret = GNUNET_strdup (out);
  }
  else
  {
    /* need to prefix with space */
    ret = GNUNET_malloc (strlen (out) + 2);
    strcpy (ret, " ");
    strcat (ret, out);
  }
  GNUNET_free (out);
  return ret;
}

#define GNUNET_FS_URI_KSK_PREFIX GNUNET_FS_URI_PREFIX GNUNET_FS_URI_KSK_INFIX

/**
 * Parse a KSK URI.
 *
 * @param s an uri string
 * @param emsg where to store the parser error message (if any)
 * @return NULL on error, otherwise the KSK URI
 */
static struct GNUNET_FS_Uri *
uri_ksk_parse (const char *s,
               char **emsg)
{
  struct GNUNET_FS_Uri *ret;
  char **keywords;
  unsigned int pos;
  int max;
  int iret;
  int i;
  size_t slen;
  char *dup;
  int saw_quote;

  slen = strlen (s);
  pos = strlen (GNUNET_FS_URI_KSK_PREFIX);
  if ((slen <= pos) || (0 != strncmp (s, GNUNET_FS_URI_KSK_PREFIX, pos)))
    return NULL;                /* not KSK URI */
  if ((s[slen - 1] == '+') || (s[pos] == '+'))
  {
    *emsg =
        GNUNET_strdup (_("Malformed KSK URI (must not begin or end with `+')"));
    return NULL;
  }
  max = 1;
  saw_quote = 0;
  for (i = pos; i < slen; i++)
  {
    if ((s[i] == '%') && (&s[i] == strstr (&s[i], "%22")))
    {
      saw_quote = (saw_quote + 1) % 2;
      i += 3;
      continue;
    }
    if ((s[i] == '+') && (saw_quote == 0))
    {
      max++;
      if (s[i - 1] == '+')
      {
        *emsg = GNUNET_strdup (_("Malformed KSK URI (`++' not allowed)"));
        return NULL;
      }
    }
  }
  if (saw_quote == 1)
  {
    *emsg = GNUNET_strdup (_("Malformed KSK URI (quotes not balanced)"));
    return NULL;
  }
  iret = max;
  dup = GNUNET_strdup (s);
  keywords = GNUNET_malloc (max * sizeof (char *));
  for (i = slen - 1; i >= (int) pos; i--)
  {
    if ((s[i] == '%') && (&s[i] == strstr (&s[i], "%22")))
    {
      saw_quote = (saw_quote + 1) % 2;
      continue;
    }
    if ((dup[i] == '+') && (saw_quote == 0))
    {
      keywords[--max] = percent_decode_keyword (&dup[i + 1], emsg);
      if (NULL == keywords[max])
        goto CLEANUP;
      dup[i] = '\0';
    }
  }
  keywords[--max] = percent_decode_keyword (&dup[pos], emsg);
  if (NULL == keywords[max])
    goto CLEANUP;
  GNUNET_assert (0 == max);
  GNUNET_free (dup);
  ret = GNUNET_new (struct GNUNET_FS_Uri);
  ret->type = GNUNET_FS_URI_KSK;
  ret->data.ksk.keywordCount = iret;
  ret->data.ksk.keywords = keywords;
  return ret;
CLEANUP:
  for (i = 0; i < max; i++)
    GNUNET_free_non_null (keywords[i]);
  GNUNET_free (keywords);
  GNUNET_free (dup);
  return NULL;
}


#define GNUNET_FS_URI_SKS_PREFIX GNUNET_FS_URI_PREFIX GNUNET_FS_URI_SKS_INFIX

/**
 * Parse an SKS URI.
 *
 * @param s an uri string
 * @param emsg where to store the parser error message (if any)
 * @return NULL on error, SKS URI otherwise
 */
static struct GNUNET_FS_Uri *
uri_sks_parse (const char *s,
               char **emsg)
{
  struct GNUNET_FS_Uri *ret;
  struct GNUNET_CRYPTO_EcdsaPublicKey ns;
  size_t pos;
  char *end;

  pos = strlen (GNUNET_FS_URI_SKS_PREFIX);
  if ((strlen (s) <= pos) || (0 != strncmp (s, GNUNET_FS_URI_SKS_PREFIX, pos)))
    return NULL;                /* not an SKS URI */
  end = strchr (&s[pos], '/');
  if ( (NULL == end) ||
       (GNUNET_OK !=
	GNUNET_STRINGS_string_to_data (&s[pos],
				       end - &s[pos],
				       &ns,
				       sizeof (ns))) )
  {
    *emsg = GNUNET_strdup (_("Malformed SKS URI (wrong syntax)"));
    return NULL; /* malformed */
  }
  end++; /* skip over '/' */
  ret = GNUNET_new (struct GNUNET_FS_Uri);
  ret->type = GNUNET_FS_URI_SKS;
  ret->data.sks.ns = ns;
  ret->data.sks.identifier = GNUNET_strdup (end);
  return ret;
}

#define GNUNET_FS_URI_CHK_PREFIX GNUNET_FS_URI_PREFIX GNUNET_FS_URI_CHK_INFIX


/**
 * Parse a CHK URI.
 *
 * @param s an uri string
 * @param emsg where to store the parser error message (if any)
 * @return NULL on error, CHK URI otherwise
 */
static struct GNUNET_FS_Uri *
uri_chk_parse (const char *s,
               char **emsg)
{
  struct GNUNET_FS_Uri *ret;
  struct FileIdentifier fi;
  unsigned int pos;
  unsigned long long flen;
  size_t slen;
  char h1[sizeof (struct GNUNET_CRYPTO_HashAsciiEncoded)];
  char h2[sizeof (struct GNUNET_CRYPTO_HashAsciiEncoded)];

  slen = strlen (s);
  pos = strlen (GNUNET_FS_URI_CHK_PREFIX);
  if ((slen < pos + 2 * sizeof (struct GNUNET_CRYPTO_HashAsciiEncoded) + 1) ||
      (0 != strncmp (s, GNUNET_FS_URI_CHK_PREFIX, pos)))
    return NULL;                /* not a CHK URI */
  if ((s[pos + sizeof (struct GNUNET_CRYPTO_HashAsciiEncoded) - 1] != '.') ||
      (s[pos + sizeof (struct GNUNET_CRYPTO_HashAsciiEncoded) * 2 - 1] != '.'))
  {
    *emsg = GNUNET_strdup (_("Malformed CHK URI (wrong syntax)"));
    return NULL;
  }
  memcpy (h1, &s[pos], sizeof (struct GNUNET_CRYPTO_HashAsciiEncoded));
  h1[sizeof (struct GNUNET_CRYPTO_HashAsciiEncoded) - 1] = '\0';
  memcpy (h2, &s[pos + sizeof (struct GNUNET_CRYPTO_HashAsciiEncoded)],
          sizeof (struct GNUNET_CRYPTO_HashAsciiEncoded));
  h2[sizeof (struct GNUNET_CRYPTO_HashAsciiEncoded) - 1] = '\0';

  if ((GNUNET_OK != GNUNET_CRYPTO_hash_from_string (h1, &fi.chk.key)) ||
      (GNUNET_OK != GNUNET_CRYPTO_hash_from_string (h2, &fi.chk.query)) ||
      (1 !=
       SSCANF (&s[pos + sizeof (struct GNUNET_CRYPTO_HashAsciiEncoded) * 2],
               "%llu", &flen)))
  {
    *emsg = GNUNET_strdup (_("Malformed CHK URI (failed to decode CHK)"));
    return NULL;
  }
  fi.file_length = GNUNET_htonll (flen);
  ret = GNUNET_new (struct GNUNET_FS_Uri);
  ret->type = GNUNET_FS_URI_CHK;
  ret->data.chk = fi;
  return ret;
}


GNUNET_NETWORK_STRUCT_BEGIN
/**
 * Structure that defines how the contents of a location URI must be
 * assembled in memory to create or verify the signature of a location
 * URI.
 */
struct LocUriAssembly
{
  /**
   * What is being signed (rest of this struct).
   */
  struct GNUNET_CRYPTO_EccSignaturePurpose purpose;

  /**
   * Expiration time of the offer.
   */
  struct GNUNET_TIME_AbsoluteNBO exptime;

  /**
   * File being offered.
   */
  struct FileIdentifier fi;

  /**
   * Peer offering the file.
   */
  struct GNUNET_PeerIdentity peer;

};
GNUNET_NETWORK_STRUCT_END


#define GNUNET_FS_URI_LOC_PREFIX GNUNET_FS_URI_PREFIX GNUNET_FS_URI_LOC_INFIX

#define SIGNATURE_ASCII_LENGTH 103

/**
 * Parse a LOC URI.
 * Also verifies validity of the location URI.
 *
 * @param s an uri string
 * @param emsg where to store the parser error message (if any)
 * @return NULL on error, valid LOC URI otherwise
 */
static struct GNUNET_FS_Uri *
uri_loc_parse (const char *s,
               char **emsg)
{
  struct GNUNET_FS_Uri *uri;
  char h1[sizeof (struct GNUNET_CRYPTO_HashAsciiEncoded)];
  char h2[sizeof (struct GNUNET_CRYPTO_HashAsciiEncoded)];
  unsigned int pos;
  unsigned int npos;
  unsigned long long exptime;
  unsigned long long flen;
  struct GNUNET_TIME_Absolute et;
  struct GNUNET_CRYPTO_EddsaSignature sig;
  struct LocUriAssembly ass;
  size_t slen;

  slen = strlen (s);
  pos = strlen (GNUNET_FS_URI_LOC_PREFIX);
  if ((slen < pos + 2 * sizeof (struct GNUNET_CRYPTO_HashAsciiEncoded) + 1) ||
      (0 != strncmp (s, GNUNET_FS_URI_LOC_PREFIX, pos)))
    return NULL;                /* not a LOC URI */
  if ((s[pos + sizeof (struct GNUNET_CRYPTO_HashAsciiEncoded) - 1] != '.') ||
      (s[pos + sizeof (struct GNUNET_CRYPTO_HashAsciiEncoded) * 2 - 1] != '.'))
  {
    *emsg = GNUNET_strdup (_("LOC URI malformed (wrong syntax)"));
    return NULL;
  }
  memcpy (h1, &s[pos], sizeof (struct GNUNET_CRYPTO_HashAsciiEncoded));
  h1[sizeof (struct GNUNET_CRYPTO_HashAsciiEncoded) - 1] = '\0';
  memcpy (h2, &s[pos + sizeof (struct GNUNET_CRYPTO_HashAsciiEncoded)],
          sizeof (struct GNUNET_CRYPTO_HashAsciiEncoded));
  h2[sizeof (struct GNUNET_CRYPTO_HashAsciiEncoded) - 1] = '\0';

  if ((GNUNET_OK != GNUNET_CRYPTO_hash_from_string (h1, &ass.fi.chk.key)) ||
      (GNUNET_OK != GNUNET_CRYPTO_hash_from_string (h2, &ass.fi.chk.query)) ||
      (1 !=
       SSCANF (&s[pos + sizeof (struct GNUNET_CRYPTO_HashAsciiEncoded) * 2],
               "%llu", &flen)))
  {
    *emsg = GNUNET_strdup (_("LOC URI malformed (no CHK)"));
    return NULL;
  }
  ass.fi.file_length = GNUNET_htonll (flen);

  npos = pos + sizeof (struct GNUNET_CRYPTO_HashAsciiEncoded) * 2;
  while ((s[npos] != '\0') && (s[npos] != '.'))
    npos++;
  if (s[npos] == '\0')
  {
    *emsg = GNUNET_strdup (_("LOC URI malformed (missing LOC)"));
    goto ERR;
  }
  npos++;
  if ( (strlen (&s[npos]) <= GNUNET_CRYPTO_PKEY_ASCII_LENGTH + 1) ||
       ('.' != s[npos+GNUNET_CRYPTO_PKEY_ASCII_LENGTH]) )
  {
    *emsg =
      GNUNET_strdup (_("LOC URI malformed (wrong syntax for public key)"));
  }
  if (GNUNET_OK !=
      GNUNET_CRYPTO_eddsa_public_key_from_string (&s[npos],
                                                  GNUNET_CRYPTO_PKEY_ASCII_LENGTH,
                                                  &ass.peer.public_key))
  {
    *emsg =
        GNUNET_strdup (_("LOC URI malformed (could not decode public key)"));
    goto ERR;
  }
  npos += GNUNET_CRYPTO_PKEY_ASCII_LENGTH;
  if (s[npos++] != '.')
  {
    *emsg = GNUNET_strdup (_("LOC URI malformed (could not find signature)"));
    goto ERR;
  }
  if ( (strlen (&s[npos]) <= SIGNATURE_ASCII_LENGTH + 1) ||
       ('.' != s[npos + SIGNATURE_ASCII_LENGTH]) )
  {
    *emsg = GNUNET_strdup (_("LOC URI malformed (wrong syntax for signature)"));
    goto ERR;
  }
  if (GNUNET_OK !=
      GNUNET_STRINGS_string_to_data (&s[npos],
                                     SIGNATURE_ASCII_LENGTH,
                                     &sig,
                                     sizeof (struct GNUNET_CRYPTO_EddsaSignature)))
  {
    *emsg = GNUNET_strdup (_("LOC URI malformed (could not decode signature)"));
    goto ERR;
  }
  npos += SIGNATURE_ASCII_LENGTH;
  if (s[npos++] != '.')
  {
    *emsg = GNUNET_strdup (_("LOC URI malformed (wrong syntax for expiration time)"));
    goto ERR;
  }
  if (1 != SSCANF (&s[npos], "%llu", &exptime))
  {
    *emsg =
        GNUNET_strdup (_("LOC URI malformed (could not parse expiration time)"));
    goto ERR;
  }
  ass.purpose.size = htonl (sizeof (struct LocUriAssembly));
  ass.purpose.purpose = htonl (GNUNET_SIGNATURE_PURPOSE_PEER_PLACEMENT);
  et.abs_value_us = exptime * 1000LL * 1000LL;
  ass.exptime = GNUNET_TIME_absolute_hton (et);
  if (GNUNET_OK !=
      GNUNET_CRYPTO_eddsa_verify (GNUNET_SIGNATURE_PURPOSE_PEER_PLACEMENT,
                                  &ass.purpose, &sig, &ass.peer.public_key))
  {
    *emsg =
        GNUNET_strdup (_("LOC URI malformed (signature failed validation)"));
    goto ERR;
  }
  uri = GNUNET_new (struct GNUNET_FS_Uri);
  uri->type = GNUNET_FS_URI_LOC;
  uri->data.loc.fi = ass.fi;
  uri->data.loc.peer = ass.peer;
  uri->data.loc.expirationTime = et;
  uri->data.loc.contentSignature = sig;

  return uri;
ERR:
  return NULL;
}


/**
 * Convert a UTF-8 String to a URI.
 *
 * @param uri string to parse
 * @param emsg where to store the parser error message (if any)
 * @return NULL on error
 */
struct GNUNET_FS_Uri *
GNUNET_FS_uri_parse (const char *uri,
                     char **emsg)
{
  struct GNUNET_FS_Uri *ret;
  char *msg;

  if (NULL == uri)
  {
    GNUNET_break (0);
    if (NULL != emsg)
      *emsg = GNUNET_strdup (_("invalid argument"));
    return NULL;
  }
  if (NULL == emsg)
    emsg = &msg;
  *emsg = NULL;
  if ((NULL != (ret = uri_chk_parse (uri, emsg))) ||
      (NULL != (ret = uri_ksk_parse (uri, emsg))) ||
      (NULL != (ret = uri_sks_parse (uri, emsg))) ||
      (NULL != (ret = uri_loc_parse (uri, emsg))))
    return ret;
  if (NULL == *emsg)
    *emsg = GNUNET_strdup (_("Unrecognized URI type"));
  if (emsg == &msg)
    GNUNET_free (msg);
  return NULL;
}


/**
 * Free URI.
 *
 * @param uri uri to free
 */
void
GNUNET_FS_uri_destroy (struct GNUNET_FS_Uri *uri)
{
  unsigned int i;

  switch (uri->type)
  {
  case GNUNET_FS_URI_KSK:
    for (i = 0; i < uri->data.ksk.keywordCount; i++)
      GNUNET_free (uri->data.ksk.keywords[i]);
    GNUNET_array_grow (uri->data.ksk.keywords, uri->data.ksk.keywordCount, 0);
    break;
  case GNUNET_FS_URI_SKS:
    GNUNET_free (uri->data.sks.identifier);
    break;
  case GNUNET_FS_URI_LOC:
    break;
  default:
    /* do nothing */
    break;
  }
  GNUNET_free (uri);
}


/**
 * How many keywords are ANDed in this keyword URI?
 *
 * @param uri ksk uri to get the number of keywords from
 * @return 0 if this is not a keyword URI
 */
unsigned int
GNUNET_FS_uri_ksk_get_keyword_count (const struct GNUNET_FS_Uri *uri)
{
  if (uri->type != GNUNET_FS_URI_KSK)
    return 0;
  return uri->data.ksk.keywordCount;
}


/**
 * Iterate over all keywords in this keyword URI.
 *
 * @param uri ksk uri to get the keywords from
 * @param iterator function to call on each keyword
 * @param iterator_cls closure for iterator
 * @return -1 if this is not a keyword URI, otherwise number of
 *   keywords iterated over until iterator aborted
 */
int
GNUNET_FS_uri_ksk_get_keywords (const struct GNUNET_FS_Uri *uri,
                                GNUNET_FS_KeywordIterator iterator,
                                void *iterator_cls)
{
  unsigned int i;
  char *keyword;

  if (uri->type != GNUNET_FS_URI_KSK)
    return -1;
  if (NULL == iterator)
    return uri->data.ksk.keywordCount;
  for (i = 0; i < uri->data.ksk.keywordCount; i++)
  {
    keyword = uri->data.ksk.keywords[i];
    /* first character of keyword indicates
     * if it is mandatory or not */
    if (GNUNET_OK != iterator (iterator_cls, &keyword[1], keyword[0] == '+'))
      return i;
  }
  return i;
}


/**
 * Add the given keyword to the set of keywords represented by the URI.
 * Does nothing if the keyword is already present.
 *
 * @param uri ksk uri to modify
 * @param keyword keyword to add
 * @param is_mandatory is this keyword mandatory?
 */
void
GNUNET_FS_uri_ksk_add_keyword (struct GNUNET_FS_Uri *uri,
                               const char *keyword,
                               int is_mandatory)
{
  unsigned int i;
  const char *old;
  char *n;

  GNUNET_assert (uri->type == GNUNET_FS_URI_KSK);
  for (i = 0; i < uri->data.ksk.keywordCount; i++)
  {
    old = uri->data.ksk.keywords[i];
    if (0 == strcmp (&old[1], keyword))
      return;
  }
  GNUNET_asprintf (&n, is_mandatory ? "+%s" : " %s", keyword);
  GNUNET_array_append (uri->data.ksk.keywords, uri->data.ksk.keywordCount, n);
}


/**
 * Remove the given keyword from the set of keywords represented by the URI.
 * Does nothing if the keyword is not present.
 *
 * @param uri ksk uri to modify
 * @param keyword keyword to add
 */
void
GNUNET_FS_uri_ksk_remove_keyword (struct GNUNET_FS_Uri *uri,
                                  const char *keyword)
{
  unsigned int i;
  char *old;

  GNUNET_assert (uri->type == GNUNET_FS_URI_KSK);
  for (i = 0; i < uri->data.ksk.keywordCount; i++)
  {
    old = uri->data.ksk.keywords[i];
    if (0 == strcmp (&old[1], keyword))
    {
      uri->data.ksk.keywords[i] =
          uri->data.ksk.keywords[uri->data.ksk.keywordCount - 1];
      GNUNET_array_grow (uri->data.ksk.keywords, uri->data.ksk.keywordCount,
                         uri->data.ksk.keywordCount - 1);
      GNUNET_free (old);
      return;
    }
  }
}


/**
 * Obtain the identity of the peer offering the data
 *
 * @param uri the location URI to inspect
 * @param peer where to store the identify of the peer (presumably) offering the content
 * @return #GNUNET_SYSERR if this is not a location URI, otherwise #GNUNET_OK
 */
int
GNUNET_FS_uri_loc_get_peer_identity (const struct GNUNET_FS_Uri *uri,
                                     struct GNUNET_PeerIdentity *peer)
{
  if (uri->type != GNUNET_FS_URI_LOC)
    return GNUNET_SYSERR;
  *peer = uri->data.loc.peer;
  return GNUNET_OK;
}


/**
 * Obtain the expiration of the LOC URI.
 *
 * @param uri location URI to get the expiration from
 * @return expiration time of the URI
 */
struct GNUNET_TIME_Absolute
GNUNET_FS_uri_loc_get_expiration (const struct GNUNET_FS_Uri *uri)
{
  GNUNET_assert (uri->type == GNUNET_FS_URI_LOC);
  return uri->data.loc.expirationTime;
}


/**
 * Obtain the URI of the content itself.
 *
 * @param uri location URI to get the content URI from
 * @return NULL if argument is not a location URI
 */
struct GNUNET_FS_Uri *
GNUNET_FS_uri_loc_get_uri (const struct GNUNET_FS_Uri *uri)
{
  struct GNUNET_FS_Uri *ret;

  if (uri->type != GNUNET_FS_URI_LOC)
    return NULL;
  ret = GNUNET_new (struct GNUNET_FS_Uri);
  ret->type = GNUNET_FS_URI_CHK;
  ret->data.chk = uri->data.loc.fi;
  return ret;
}


/**
 * Construct a location URI (this peer will be used for the location).
 * This function should only be called from within gnunet-service-fs,
 * as it requires the peer's private key which is generally unavailable
 * to processes directly under the user's control.  However, for
 * testing and as it logically fits under URIs, it is in this API.
 *
 * @param base_uri content offered by the sender
 * @param sign_key private key of the peer
 * @param expiration_time how long will the content be offered?
 * @return the location URI, NULL on error
 */
struct GNUNET_FS_Uri *
GNUNET_FS_uri_loc_create (const struct GNUNET_FS_Uri *base_uri,
                          const struct GNUNET_CRYPTO_EddsaPrivateKey *sign_key,
                          struct GNUNET_TIME_Absolute expiration_time)
{
  struct GNUNET_FS_Uri *uri;
  struct GNUNET_CRYPTO_EddsaPublicKey my_public_key;
  struct LocUriAssembly ass;
  struct GNUNET_TIME_Absolute et;

  if (GNUNET_FS_URI_CHK != base_uri->type)
    return NULL;
  /* we round expiration time to full seconds for SKS URIs */
  et.abs_value_us = (expiration_time.abs_value_us / 1000000LL) * 1000000LL;
  GNUNET_CRYPTO_eddsa_key_get_public (sign_key,
                                      &my_public_key);
  ass.purpose.size = htonl (sizeof (struct LocUriAssembly));
  ass.purpose.purpose = htonl (GNUNET_SIGNATURE_PURPOSE_PEER_PLACEMENT);
  ass.exptime = GNUNET_TIME_absolute_hton (et);
  ass.fi = base_uri->data.chk;
  ass.peer.public_key = my_public_key;
  uri = GNUNET_new (struct GNUNET_FS_Uri);
  uri->type = GNUNET_FS_URI_LOC;
  uri->data.loc.fi = base_uri->data.chk;
  uri->data.loc.expirationTime = et;
  uri->data.loc.peer.public_key = my_public_key;
  GNUNET_assert (GNUNET_OK ==
                 GNUNET_CRYPTO_eddsa_sign (sign_key,
                                           &ass.purpose,
                                           &uri->data.loc.contentSignature));
  return uri;
}


/**
 * Create an SKS URI from a namespace ID and an identifier.
 *
 * @param ns namespace ID
 * @param id identifier
 * @return an FS URI for the given namespace and identifier
 */
struct GNUNET_FS_Uri *
GNUNET_FS_uri_sks_create (const struct GNUNET_CRYPTO_EcdsaPublicKey *ns,
			  const char *id)
{
  struct GNUNET_FS_Uri *ns_uri;

  ns_uri = GNUNET_new (struct GNUNET_FS_Uri);
  ns_uri->type = GNUNET_FS_URI_SKS;
  ns_uri->data.sks.ns = *ns;
  ns_uri->data.sks.identifier = GNUNET_strdup (id);
  return ns_uri;
}


/**
 * Merge the sets of keywords from two KSK URIs.
 * (useful for merging the canonicalized keywords with
 * the original keywords for sharing).
 *
 * @param u1 first uri
 * @param u2 second uri
 * @return merged URI, NULL on error
 */
struct GNUNET_FS_Uri *
GNUNET_FS_uri_ksk_merge (const struct GNUNET_FS_Uri *u1,
                         const struct GNUNET_FS_Uri *u2)
{
  struct GNUNET_FS_Uri *ret;
  unsigned int kc;
  unsigned int i;
  unsigned int j;
  int found;
  const char *kp;
  char **kl;

  if ((u1 == NULL) && (u2 == NULL))
    return NULL;
  if (u1 == NULL)
    return GNUNET_FS_uri_dup (u2);
  if (u2 == NULL)
    return GNUNET_FS_uri_dup (u1);
  if ((u1->type != GNUNET_FS_URI_KSK) || (u2->type != GNUNET_FS_URI_KSK))
  {
    GNUNET_break (0);
    return NULL;
  }
  kc = u1->data.ksk.keywordCount;
  kl = GNUNET_malloc ((kc + u2->data.ksk.keywordCount) * sizeof (char *));
  for (i = 0; i < u1->data.ksk.keywordCount; i++)
    kl[i] = GNUNET_strdup (u1->data.ksk.keywords[i]);
  for (i = 0; i < u2->data.ksk.keywordCount; i++)
  {
    kp = u2->data.ksk.keywords[i];
    found = 0;
    for (j = 0; j < u1->data.ksk.keywordCount; j++)
      if (0 == strcmp (kp + 1, kl[j] + 1))
      {
        found = 1;
        if (kp[0] == '+')
          kl[j][0] = '+';
        break;
      }
    if (0 == found)
      kl[kc++] = GNUNET_strdup (kp);
  }
  ret = GNUNET_new (struct GNUNET_FS_Uri);
  ret->type = GNUNET_FS_URI_KSK;
  ret->data.ksk.keywordCount = kc;
  ret->data.ksk.keywords = kl;
  return ret;
}


/**
 * Duplicate URI.
 *
 * @param uri the URI to duplicate
 * @return copy of the URI
 */
struct GNUNET_FS_Uri *
GNUNET_FS_uri_dup (const struct GNUNET_FS_Uri *uri)
{
  struct GNUNET_FS_Uri *ret;
  unsigned int i;

  if (uri == NULL)
    return NULL;
  ret = GNUNET_new (struct GNUNET_FS_Uri);
  memcpy (ret, uri, sizeof (struct GNUNET_FS_Uri));
  switch (ret->type)
  {
  case GNUNET_FS_URI_KSK:
    if (ret->data.ksk.keywordCount >=
        GNUNET_MAX_MALLOC_CHECKED / sizeof (char *))
    {
      GNUNET_break (0);
      GNUNET_free (ret);
      return NULL;
    }
    if (ret->data.ksk.keywordCount > 0)
    {
      ret->data.ksk.keywords =
          GNUNET_malloc (ret->data.ksk.keywordCount * sizeof (char *));
      for (i = 0; i < ret->data.ksk.keywordCount; i++)
        ret->data.ksk.keywords[i] = GNUNET_strdup (uri->data.ksk.keywords[i]);
    }
    else
      ret->data.ksk.keywords = NULL;    /* just to be sure */
    break;
  case GNUNET_FS_URI_SKS:
    ret->data.sks.identifier = GNUNET_strdup (uri->data.sks.identifier);
    break;
  case GNUNET_FS_URI_LOC:
    break;
  default:
    break;
  }
  return ret;
}


/**
 * Create an FS URI from a single user-supplied string of keywords.
 * The string is broken up at spaces into individual keywords.
 * Keywords that start with "+" are mandatory.  Double-quotes can
 * be used to prevent breaking up strings at spaces (and also
 * to specify non-mandatory keywords starting with "+").
 *
 * Keywords must contain a balanced number of double quotes and
 * double quotes can not be used in the actual keywords (for
 * example, the string '""foo bar""' will be turned into two
 * "OR"ed keywords 'foo' and 'bar', not into '"foo bar"'.
 *
 * @param keywords the keyword string
 * @param emsg where to store an error message
 * @return an FS URI for the given keywords, NULL
 *  if keywords is not legal (i.e. empty).
 */
struct GNUNET_FS_Uri *
GNUNET_FS_uri_ksk_create (const char *keywords,
                          char **emsg)
{
  char **keywordarr;
  unsigned int num_Words;
  int inWord;
  char *pos;
  struct GNUNET_FS_Uri *uri;
  char *searchString;
  int saw_quote;

  if (keywords == NULL)
  {
    *emsg = GNUNET_strdup (_("No keywords specified!\n"));
    GNUNET_break (0);
    return NULL;
  }
  searchString = GNUNET_strdup (keywords);
  num_Words = 0;
  inWord = 0;
  saw_quote = 0;
  pos = searchString;
  while ('\0' != *pos)
  {
    if ((saw_quote == 0) && (isspace ((unsigned char) *pos)))
    {
      inWord = 0;
    }
    else if (0 == inWord)
    {
      inWord = 1;
      ++num_Words;
    }
    if ('"' == *pos)
      saw_quote = (saw_quote + 1) % 2;
    pos++;
  }
  if (num_Words == 0)
  {
    GNUNET_free (searchString);
    *emsg = GNUNET_strdup (_("No keywords specified!\n"));
    return NULL;
  }
  if (saw_quote != 0)
  {
    GNUNET_free (searchString);
    *emsg = GNUNET_strdup (_("Number of double-quotes not balanced!\n"));
    return NULL;
  }
  keywordarr = GNUNET_malloc (num_Words * sizeof (char *));
  num_Words = 0;
  inWord = 0;
  pos = searchString;
  while ('\0' != *pos)
  {
    if ((saw_quote == 0) && (isspace ((unsigned char) *pos)))
    {
      inWord = 0;
      *pos = '\0';
    }
    else if (0 == inWord)
    {
      keywordarr[num_Words] = pos;
      inWord = 1;
      ++num_Words;
    }
    if ('"' == *pos)
      saw_quote = (saw_quote + 1) % 2;
    pos++;
  }
  uri =
      GNUNET_FS_uri_ksk_create_from_args (num_Words,
                                          (const char **) keywordarr);
  GNUNET_free (keywordarr);
  GNUNET_free (searchString);
  return uri;
}


/**
 * Create an FS URI from a user-supplied command line of keywords.
 * Arguments should start with "+" to indicate mandatory
 * keywords.
 *
 * @param argc number of keywords
 * @param argv keywords (double quotes are not required for
 *             keywords containing spaces; however, double
 *             quotes are required for keywords starting with
 *             "+"); there is no mechanism for having double
 *             quotes in the actual keywords (if the user
 *             did specifically specify double quotes, the
 *             caller should convert each double quote
 *             into two single quotes).
 * @return an FS URI for the given keywords, NULL
 *  if keywords is not legal (i.e. empty).
 */
struct GNUNET_FS_Uri *
GNUNET_FS_uri_ksk_create_from_args (unsigned int argc,
                                    const char **argv)
{
  unsigned int i;
  struct GNUNET_FS_Uri *uri;
  const char *keyword;
  char *val;
  const char *r;
  char *w;
  char *emsg;

  if (argc == 0)
    return NULL;
  /* allow URI to be given as one and only keyword and
   * handle accordingly */
  emsg = NULL;
  if ((argc == 1) && (strlen (argv[0]) > strlen (GNUNET_FS_URI_PREFIX)) &&
      (0 ==
       strncmp (argv[0], GNUNET_FS_URI_PREFIX, strlen (GNUNET_FS_URI_PREFIX)))
      && (NULL != (uri = GNUNET_FS_uri_parse (argv[0], &emsg))))
    return uri;
  GNUNET_free_non_null (emsg);
  uri = GNUNET_new (struct GNUNET_FS_Uri);
  uri->type = GNUNET_FS_URI_KSK;
  uri->data.ksk.keywordCount = argc;
  uri->data.ksk.keywords = GNUNET_malloc (argc * sizeof (char *));
  for (i = 0; i < argc; i++)
  {
    keyword = argv[i];
    if (keyword[0] == '+')
      val = GNUNET_strdup (keyword);
    else
      GNUNET_asprintf (&val, " %s", keyword);
    r = val;
    w = val;
    while ('\0' != *r)
    {
      if ('"' == *r)
        r++;
      else
        *(w++) = *(r++);
    }
    *w = '\0';
    uri->data.ksk.keywords[i] = val;
  }
  return uri;
}


/**
 * Test if two URIs are equal.
 *
 * @param u1 one of the URIs
 * @param u2 the other URI
 * @return #GNUNET_YES if the URIs are equal
 */
int
GNUNET_FS_uri_test_equal (const struct GNUNET_FS_Uri *u1,
                          const struct GNUNET_FS_Uri *u2)
{
  int ret;
  unsigned int i;
  unsigned int j;

  GNUNET_assert (u1 != NULL);
  GNUNET_assert (u2 != NULL);
  if (u1->type != u2->type)
    return GNUNET_NO;
  switch (u1->type)
  {
  case GNUNET_FS_URI_CHK:
    if (0 ==
        memcmp (&u1->data.chk, &u2->data.chk, sizeof (struct FileIdentifier)))
      return GNUNET_YES;
    return GNUNET_NO;
  case GNUNET_FS_URI_SKS:
    if ((0 ==
         memcmp (&u1->data.sks.ns, &u2->data.sks.ns,
                 sizeof (struct GNUNET_CRYPTO_EcdsaPublicKey))) &&
        (0 == strcmp (u1->data.sks.identifier, u2->data.sks.identifier)))

      return GNUNET_YES;
    return GNUNET_NO;
  case GNUNET_FS_URI_KSK:
    if (u1->data.ksk.keywordCount != u2->data.ksk.keywordCount)
      return GNUNET_NO;
    for (i = 0; i < u1->data.ksk.keywordCount; i++)
    {
      ret = GNUNET_NO;
      for (j = 0; j < u2->data.ksk.keywordCount; j++)
      {
        if (0 == strcmp (u1->data.ksk.keywords[i], u2->data.ksk.keywords[j]))
        {
          ret = GNUNET_YES;
          break;
        }
      }
      if (ret == GNUNET_NO)
        return GNUNET_NO;
    }
    return GNUNET_YES;
  case GNUNET_FS_URI_LOC:
    if (memcmp
        (&u1->data.loc, &u2->data.loc,
         sizeof (struct FileIdentifier) +
         sizeof (struct GNUNET_CRYPTO_EcdsaPublicKey) +
         sizeof (struct GNUNET_TIME_Absolute) + sizeof (unsigned short) +
         sizeof (unsigned short)) != 0)
      return GNUNET_NO;
    return GNUNET_YES;
  default:
    return GNUNET_NO;
  }
}


/**
 * Is this a namespace URI?
 *
 * @param uri the uri to check
 * @return #GNUNET_YES if this is an SKS uri
 */
int
GNUNET_FS_uri_test_sks (const struct GNUNET_FS_Uri *uri)
{
  return uri->type == GNUNET_FS_URI_SKS;
}


/**
 * Get the ID of a namespace from the given
 * namespace URI.
 *
 * @param uri the uri to get the namespace ID from
 * @param pseudonym where to store the ID of the namespace
 * @return #GNUNET_OK on success
 */
int
GNUNET_FS_uri_sks_get_namespace (const struct GNUNET_FS_Uri *uri,
                                 struct GNUNET_CRYPTO_EcdsaPublicKey *pseudonym)
{
  if (!GNUNET_FS_uri_test_sks (uri))
  {
    GNUNET_break (0);
    return GNUNET_SYSERR;
  }
  *pseudonym = uri->data.sks.ns;
  return GNUNET_OK;
}


/**
 * Get the content identifier of an SKS URI.
 *
 * @param uri the sks uri
 * @return NULL on error (not a valid SKS URI)
 */
char *
GNUNET_FS_uri_sks_get_content_id (const struct GNUNET_FS_Uri *uri)
{
  if (!GNUNET_FS_uri_test_sks (uri))
  {
    GNUNET_break (0);
    return NULL;
  }
  return GNUNET_strdup (uri->data.sks.identifier);
}


/**
 * Is this a keyword URI?
 *
 * @param uri the uri
 * @return #GNUNET_YES if this is a KSK uri
 */
int
GNUNET_FS_uri_test_ksk (const struct GNUNET_FS_Uri *uri)
{
#if EXTRA_CHECKS
  unsigned int i;

  if (uri->type == GNUNET_FS_URI_KSK)
  {
    for (i=0;i < uri->data.ksk.keywordCount; i++)
      GNUNET_assert (uri->data.ksk.keywords[i] != NULL);
  }
#endif
  return uri->type == GNUNET_FS_URI_KSK;
}


/**
 * Is this a file (or directory) URI?
 *
 * @param uri the uri to check
 * @return #GNUNET_YES if this is a CHK uri
 */
int
GNUNET_FS_uri_test_chk (const struct GNUNET_FS_Uri *uri)
{
  return uri->type == GNUNET_FS_URI_CHK;
}


/**
 * What is the size of the file that this URI
 * refers to?
 *
 * @param uri the CHK URI to inspect
 * @return size of the file as specified in the CHK URI
 */
uint64_t
GNUNET_FS_uri_chk_get_file_size (const struct GNUNET_FS_Uri * uri)
{
  switch (uri->type)
  {
  case GNUNET_FS_URI_CHK:
    return GNUNET_ntohll (uri->data.chk.file_length);
  case GNUNET_FS_URI_LOC:
    return GNUNET_ntohll (uri->data.loc.fi.file_length);
  default:
    GNUNET_assert (0);
  }
  return 0;                     /* unreachable */
}


/**
 * Is this a location URI?
 *
 * @param uri the uri to check
 * @return #GNUNET_YES if this is a LOC uri
 */
int
GNUNET_FS_uri_test_loc (const struct GNUNET_FS_Uri *uri)
{
  return uri->type == GNUNET_FS_URI_LOC;
}


/**
 * Add a keyword as non-mandatory (with ' '-prefix) to the
 * given keyword list at offset 'index'.  The array is
 * guaranteed to be long enough.
 *
 * @param s keyword to add
 * @param array array to add the keyword to
 * @param index offset where to add the keyword
 */
static void
insert_non_mandatory_keyword (const char *s,
                              char **array,
                              int index)
{
  char *nkword;

  GNUNET_asprintf (&nkword,
                   " %s", /* space to mark as 'non mandatory' */
                   s);
  array[index] = nkword;
}


/**
 * Test if the given keyword @a s is already present in the
 * given array, ignoring the '+'-mandatory prefix in the array.
 *
 * @param s keyword to test
 * @param array keywords to test against, with ' ' or '+' prefix to ignore
 * @param array_length length of the @a array
 * @return #GNUNET_YES if the keyword exists, #GNUNET_NO if not
 */
static int
find_duplicate (const char *s,
                const char **array,
                int array_length)
{
  int j;

  for (j = array_length - 1; j >= 0; j--)
    if (0 == strcmp (&array[j][1], s))
      return GNUNET_YES;
  return GNUNET_NO;
}


/**
 * FIXME: comment
 */
static char *
normalize_metadata (enum EXTRACTOR_MetaFormat format,
                    const char *data,
                    size_t data_len)
{
  uint8_t *free_str = NULL;
  uint8_t *str_to_normalize = (uint8_t *) data;
  uint8_t *normalized;
  size_t r_len;
  if (str_to_normalize == NULL)
    return NULL;
  /* Don't trust libextractor */
  if (format == EXTRACTOR_METAFORMAT_UTF8)
  {
    free_str = (uint8_t *) u8_check ((const uint8_t *) data, data_len);
    if (free_str == NULL)
      free_str = NULL;
    else
      format = EXTRACTOR_METAFORMAT_C_STRING;
  }
  if (format == EXTRACTOR_METAFORMAT_C_STRING)
  {
    free_str = u8_strconv_from_encoding (data, locale_charset (), iconveh_escape_sequence);
    if (free_str == NULL)
      return NULL;
  }

  normalized = u8_tolower (str_to_normalize, strlen ((char *) str_to_normalize), NULL, UNINORM_NFD, NULL, &r_len);
  /* free_str is allocated by libunistring internally, use free() */
  if (free_str != NULL)
    free (free_str);
  if (normalized != NULL)
  {
    /* u8_tolower allocates a non-NULL-terminated string! */
    free_str = GNUNET_malloc (r_len + 1);
    memcpy (free_str, normalized, r_len);
    free_str[r_len] = '\0';
    free (normalized);
    normalized = free_str;
  }
  return (char *) normalized;
}


/**
 * Counts the number of UTF-8 characters (not bytes) in the string,
 * returns that count.
 */
static size_t
u8_strcount (const uint8_t *s)
{
  size_t count;
  ucs4_t c;
  GNUNET_assert (s != NULL);
  if (s[0] == 0)
    return 0;
  for (count = 0; s != NULL; count++)
    s = u8_next (&c, s);
  return count - 1;
}


/**
 * Break the filename up by matching [], () and {} pairs to make
 * keywords. In case of nesting parentheses only the inner pair counts.
 * You can't escape parentheses to scan something like "[blah\{foo]" to
 * make a "blah{foo" keyword, this function is only a heuristic!
 *
 * @param s string to break down.
 * @param array array to fill with enclosed tokens. If NULL, then tokens
 *        are only counted.
 * @param index index at which to start filling the array (entries prior
 *        to it are used to check for duplicates). ignored if @a array == NULL.
 * @return number of tokens counted (including duplicates), or number of
 *         tokens extracted (excluding duplicates). 0 if there are no
 *         matching parens in the string (when counting), or when all tokens
 *         were duplicates (when extracting).
 */
static int
get_keywords_from_parens (const char *s,
                          char **array,
                          int index)
{
  int count = 0;
  char *open_paren;
  char *close_paren;
  char *ss;
  char tmp;

  if (NULL == s)
    return 0;
  ss = GNUNET_strdup (s);
  open_paren = ss - 1;
  while (NULL != (open_paren = strpbrk (open_paren + 1, "[{(")))
  {
    int match = 0;

    close_paren = strpbrk (open_paren + 1, "]})");
    if (NULL == close_paren)
      continue;
    switch (open_paren[0])
    {
    case '[':
      if (']' == close_paren[0])
        match = 1;
      break;
    case '{':
      if ('}' == close_paren[0])
        match = 1;
      break;
    case '(':
      if (')' == close_paren[0])
        match = 1;
      break;
    default:
      break;
    }
    if (match && (close_paren - open_paren > 1))
    {
      tmp = close_paren[0];
      close_paren[0] = '\0';
      /* Keywords must be at least 3 characters long */
      if (u8_strcount ((const uint8_t *) &open_paren[1]) <= 2)
      {
        close_paren[0] = tmp;
        continue;
      }
      if (NULL != array)
      {
        char *normalized;
        if (GNUNET_NO == find_duplicate ((const char *) &open_paren[1],
            (const char **) array, index + count))
        {
	  insert_non_mandatory_keyword ((const char *) &open_paren[1], array,
					index + count);
          count++;
        }
        normalized = normalize_metadata (EXTRACTOR_METAFORMAT_UTF8,
            &open_paren[1], close_paren - &open_paren[1]);
        if (normalized != NULL)
        {
          if (GNUNET_NO == find_duplicate ((const char *) normalized,
              (const char **) array, index + count))
          {
	    insert_non_mandatory_keyword ((const char *) normalized, array,
					  index + count);
            count++;
          }
          GNUNET_free (normalized);
        }
      }
      else
	count++;
      close_paren[0] = tmp;
    }
  }
  GNUNET_free (ss);
  return count;
}


/**
 * Where to break up keywords
 */
#define TOKENS "_. /-!?#&+@\"\'\\;:,()[]{}$<>|"

/**
 * Break the filename up by TOKENS to make
 * keywords.
 *
 * @param s string to break down.
 * @param array array to fill with tokens. If NULL, then tokens are only
 *        counted.
 * @param index index at which to start filling the array (entries prior
 *        to it are used to check for duplicates). ignored if @a array == NULL.
 * @return number of tokens (>1) counted (including duplicates), or number of
 *         tokens extracted (excluding duplicates). 0 if there are no
 *         separators in the string (when counting), or when all tokens were
 *         duplicates (when extracting).
 */
static int
get_keywords_from_tokens (const char *s,
                          char **array,
                          int index)
{
  char *p;
  char *ss;
  int seps = 0;

  ss = GNUNET_strdup (s);
  for (p = strtok (ss, TOKENS); p != NULL; p = strtok (NULL, TOKENS))
  {
    /* Keywords must be at least 3 characters long */
    if (u8_strcount ((const uint8_t *) p) <= 2)
      continue;
    if (NULL != array)
    {
      char *normalized;
      if (GNUNET_NO == find_duplicate (p, (const char **) array, index + seps))
      {
        insert_non_mandatory_keyword (p, array,
				      index + seps);
	seps++;
      }
      normalized = normalize_metadata (EXTRACTOR_METAFORMAT_UTF8,
          p, strlen (p));
      if (normalized != NULL)
      {
        if (GNUNET_NO == find_duplicate ((const char *) normalized,
            (const char **) array, index + seps))
        {
          insert_non_mandatory_keyword ((const char *) normalized, array,
				  index + seps);
          seps++;
        }
        GNUNET_free (normalized);
      }
    }
    else
      seps++;
  }
  GNUNET_free (ss);
  return seps;
}
#undef TOKENS


/**
 * Function called on each value in the meta data.
 * Adds it to the URI.
 *
 * @param cls URI to update
 * @param plugin_name name of the plugin that produced this value;
 *        special values can be used (i.e. '&lt;zlib&gt;' for zlib being
 *        used in the main libextractor library and yielding
 *        meta data).
 * @param type libextractor-type describing the meta data
 * @param format basic format information about data
 * @param data_mime_type mime-type of data (not of the original file);
 *        can be NULL (if mime-type is not known)
 * @param data actual meta-data found
 * @param data_len number of bytes in @a data
 * @return 0 (always)
 */
static int
gather_uri_data (void *cls, const char *plugin_name,
                 enum EXTRACTOR_MetaType type,
                 enum EXTRACTOR_MetaFormat format,
                 const char *data_mime_type,
                 const char *data,
                 size_t data_len)
{
  struct GNUNET_FS_Uri *uri = cls;
  char *normalized_data;
  const char *sep;

  if ((format != EXTRACTOR_METAFORMAT_UTF8) &&
      (format != EXTRACTOR_METAFORMAT_C_STRING))
    return 0;
  /* Keywords must be at least 3 characters long
   * If given non-utf8 string it will, most likely, find it to be invalid,
   * and will return the length of its valid part, skipping the keyword.
   * If it does - fix the extractor, not this check!
   */
  if (u8_strcount ((const uint8_t *) data) <= 2)
    return 0;
  if ( (EXTRACTOR_METATYPE_MIMETYPE == type) &&
       (NULL != (sep = memchr (data, '/', data_len))) &&
       (sep != data) )
  {
    char *xtra;

    GNUNET_asprintf (&xtra,
                     "mimetype:%.*s",
                     (int) (sep - data),
                     data);
    if (! find_duplicate (xtra,
                          (const char **) uri->data.ksk.keywords,
                          uri->data.ksk.keywordCount))
    {
      insert_non_mandatory_keyword (xtra,
                                    uri->data.ksk.keywords,
                                    uri->data.ksk.keywordCount);
      uri->data.ksk.keywordCount++;
    }
    GNUNET_free (xtra);
  }

  normalized_data = normalize_metadata (format, data, data_len);
  if (! find_duplicate (data,
                        (const char **) uri->data.ksk.keywords,
                        uri->data.ksk.keywordCount))
  {
    insert_non_mandatory_keyword (data,
				  uri->data.ksk.keywords, uri->data.ksk.keywordCount);
    uri->data.ksk.keywordCount++;
  }
  if (NULL != normalized_data)
    {
    if (! find_duplicate (normalized_data,
                          (const char **) uri->data.ksk.keywords,
                          uri->data.ksk.keywordCount))
    {
      insert_non_mandatory_keyword (normalized_data,
				    uri->data.ksk.keywords, uri->data.ksk.keywordCount);
      uri->data.ksk.keywordCount++;
    }
    GNUNET_free (normalized_data);
  }
  return 0;
}


/**
 * Construct a keyword-URI from meta-data (take all entries
 * in the meta-data and construct one large keyword URI
 * that lists all keywords that can be found in the meta-data).
 *
 * @param md metadata to use
 * @return NULL on error, otherwise a KSK URI
 */
struct GNUNET_FS_Uri *
GNUNET_FS_uri_ksk_create_from_meta_data (const struct GNUNET_CONTAINER_MetaData *md)
{
  struct GNUNET_FS_Uri *ret;
  char *filename;
  char *full_name = NULL;
  char *ss;
  int ent;
  int tok_keywords = 0;
  int paren_keywords = 0;

  if (NULL == md)
    return NULL;
  ret = GNUNET_new (struct GNUNET_FS_Uri);
  ret->type = GNUNET_FS_URI_KSK;
  ent = GNUNET_CONTAINER_meta_data_iterate (md, NULL, NULL);
  if (ent > 0)
  {
    full_name = GNUNET_CONTAINER_meta_data_get_first_by_types (md,
        EXTRACTOR_METATYPE_GNUNET_ORIGINAL_FILENAME, -1);
    if (NULL != full_name)
    {
      filename = full_name;
      while (NULL != (ss = strstr (filename, DIR_SEPARATOR_STR)))
        filename = ss + 1;
      tok_keywords = get_keywords_from_tokens (filename, NULL, 0);
      paren_keywords = get_keywords_from_parens (filename, NULL, 0);
    }
    /* x3 because there might be a normalized variant of every keyword,
       plus theoretically one more for mime... */
    ret->data.ksk.keywords = GNUNET_malloc
      (sizeof (char *) * (ent + tok_keywords + paren_keywords) * 3);
    GNUNET_CONTAINER_meta_data_iterate (md, &gather_uri_data, ret);
  }
  if (tok_keywords > 0)
    ret->data.ksk.keywordCount += get_keywords_from_tokens (filename,
                                                            ret->data.ksk.keywords,
                                                            ret->data.ksk.keywordCount);
  if (paren_keywords > 0)
    ret->data.ksk.keywordCount += get_keywords_from_parens (filename,
                                                            ret->data.ksk.keywords,
                                                            ret->data.ksk.keywordCount);
  if (ent > 0)
    GNUNET_free_non_null (full_name);
  return ret;
}


/**
 * In URI-encoding, does the given character
 * need to be encoded using %-encoding?
 */
static int
needs_percent (char c)
{
  return (!
          ((isalnum ((unsigned char) c)) || (c == '-') || (c == '_') ||
           (c == '.') || (c == '~')));
}


/**
 * Convert a KSK URI to a string.
 *
 * @param uri the URI to convert
 * @return NULL on error (i.e. keywordCount == 0)
 */
static char *
uri_ksk_to_string (const struct GNUNET_FS_Uri *uri)
{
  char **keywords;
  unsigned int keywordCount;
  size_t n;
  char *ret;
  unsigned int i;
  unsigned int j;
  unsigned int wpos;
  size_t slen;
  const char *keyword;

  if (uri->type != GNUNET_FS_URI_KSK)
    return NULL;
  keywords = uri->data.ksk.keywords;
  keywordCount = uri->data.ksk.keywordCount;
  n = keywordCount + strlen (GNUNET_FS_URI_PREFIX) +
      strlen (GNUNET_FS_URI_KSK_INFIX) + 1;
  for (i = 0; i < keywordCount; i++)
  {
    keyword = keywords[i];
    slen = strlen (keyword);
    n += slen;
    for (j = 0; j < slen; j++)
    {
      if ((j == 0) && (keyword[j] == ' '))
      {
        n--;
        continue;               /* skip leading space */
      }
      if (needs_percent (keyword[j]))
        n += 2;                 /* will use %-encoding */
    }
  }
  ret = GNUNET_malloc (n);
  strcpy (ret, GNUNET_FS_URI_PREFIX);
  strcat (ret, GNUNET_FS_URI_KSK_INFIX);
  wpos = strlen (ret);
  for (i = 0; i < keywordCount; i++)
  {
    keyword = keywords[i];
    slen = strlen (keyword);
    for (j = 0; j < slen; j++)
    {
      if ((j == 0) && (keyword[j] == ' '))
        continue;               /* skip leading space */
      if (needs_percent (keyword[j]))
      {
        sprintf (&ret[wpos], "%%%02X", (unsigned char) keyword[j]);
        wpos += 3;
      }
      else
      {
        ret[wpos++] = keyword[j];
      }
    }
    if (i != keywordCount - 1)
      ret[wpos++] = '+';
  }
  return ret;
}


/**
 * Convert SKS URI to a string.
 *
 * @param uri sks uri to convert
 * @return NULL on error
 */
static char *
uri_sks_to_string (const struct GNUNET_FS_Uri *uri)
{
  char *ret;
  char buf[1024];

  if (GNUNET_FS_URI_SKS != uri->type)
    return NULL;
  ret = GNUNET_STRINGS_data_to_string (&uri->data.sks.ns,
				       sizeof (struct GNUNET_CRYPTO_EcdsaPublicKey),
				       buf,
				       sizeof (buf));
  GNUNET_assert (NULL != ret);
  ret[0] = '\0';
  GNUNET_asprintf (&ret, "%s%s%s/%s", GNUNET_FS_URI_PREFIX,
                   GNUNET_FS_URI_SKS_INFIX, buf,
		   uri->data.sks.identifier);
  return ret;
}


/**
 * Convert a CHK URI to a string.
 *
 * @param uri chk uri to convert
 * @return NULL on error
 */
static char *
uri_chk_to_string (const struct GNUNET_FS_Uri *uri)
{
  const struct FileIdentifier *fi;
  char *ret;
  struct GNUNET_CRYPTO_HashAsciiEncoded keyhash;
  struct GNUNET_CRYPTO_HashAsciiEncoded queryhash;

  if (uri->type != GNUNET_FS_URI_CHK)
    return NULL;
  fi = &uri->data.chk;
  GNUNET_CRYPTO_hash_to_enc (&fi->chk.key, &keyhash);
  GNUNET_CRYPTO_hash_to_enc (&fi->chk.query, &queryhash);

  GNUNET_asprintf (&ret, "%s%s%s.%s.%llu", GNUNET_FS_URI_PREFIX,
                   GNUNET_FS_URI_CHK_INFIX, (const char *) &keyhash,
                   (const char *) &queryhash, GNUNET_ntohll (fi->file_length));
  return ret;
}


/**
 * Convert a LOC URI to a string.
 *
 * @param uri loc uri to convert
 * @return NULL on error
 */
static char *
uri_loc_to_string (const struct GNUNET_FS_Uri *uri)
{
  char *ret;
  struct GNUNET_CRYPTO_HashAsciiEncoded keyhash;
  struct GNUNET_CRYPTO_HashAsciiEncoded queryhash;
  char *peer_id;
  char peer_sig[SIGNATURE_ASCII_LENGTH + 1];

  GNUNET_CRYPTO_hash_to_enc (&uri->data.loc.fi.chk.key, &keyhash);
  GNUNET_CRYPTO_hash_to_enc (&uri->data.loc.fi.chk.query, &queryhash);
  peer_id =
    GNUNET_CRYPTO_eddsa_public_key_to_string (&uri->data.loc.peer.public_key);
  GNUNET_assert (NULL !=
                 GNUNET_STRINGS_data_to_string (&uri->data.loc.contentSignature,
                                                sizeof (struct GNUNET_CRYPTO_EddsaSignature),
                                                peer_sig,
                                                sizeof (peer_sig)));
  GNUNET_asprintf (&ret,
		   "%s%s%s.%s.%llu.%s.%s.%llu", GNUNET_FS_URI_PREFIX,
                   GNUNET_FS_URI_LOC_INFIX, (const char *) &keyhash,
                   (const char *) &queryhash,
                   (unsigned long long) GNUNET_ntohll (uri->data.loc.
                                                       fi.file_length),
                   peer_id,
                   peer_sig,
                   (unsigned long long) uri->data.loc.expirationTime.abs_value_us / 1000000LL);
  GNUNET_free (peer_id);
  return ret;
}


/**
 * Convert a URI to a UTF-8 String.
 *
 * @param uri uri to convert to a string
 * @return the UTF-8 string
 */
char *
GNUNET_FS_uri_to_string (const struct GNUNET_FS_Uri *uri)
{
  if (uri == NULL)
  {
    GNUNET_break (0);
    return NULL;
  }
  switch (uri->type)
  {
  case GNUNET_FS_URI_KSK:
    return uri_ksk_to_string (uri);
  case GNUNET_FS_URI_SKS:
    return uri_sks_to_string (uri);
  case GNUNET_FS_URI_CHK:
    return uri_chk_to_string (uri);
  case GNUNET_FS_URI_LOC:
    return uri_loc_to_string (uri);
  default:
    GNUNET_break (0);
    return NULL;
  }
}

/* end of fs_uri.c */
