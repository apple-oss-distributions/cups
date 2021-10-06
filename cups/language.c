/*
 * "$Id: language.c,v 1.16.2.1 2006/02/08 22:04:46 jlovell Exp $"
 *
 *   I18N/language support for the Common UNIX Printing System (CUPS).
 *
 *   Copyright 1997-2005 by Easy Software Products.
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Easy Software Products and are protected by Federal
 *   copyright law.  Distribution and use rights are outlined in the file
 *   "LICENSE.txt" which should have been included with this file.  If this
 *   file is missing or damaged please contact Easy Software Products
 *   at:
 *
 *       Attn: CUPS Licensing Information
 *       Easy Software Products
 *       44141 Airport View Drive, Suite 204
 *       Hollywood, Maryland 20636 USA
 *
 *       Voice: (301) 373-9600
 *       EMail: cups-info@cups.org
 *         WWW: http://www.cups.org
 *
 *   This file is subject to the Apple OS-Developed Software exception.
 *
 * Contents:
 *
 *   cupsLangEncoding()   - Return the character encoding (us-ascii, etc.)
 *                          for the given language.
 *   cupsLangFlush()      - Flush all language data out of the cache.
 *   cupsLangFree()       - Free language data.
 *   cupsLangGet()        - Get a language.
 *   _cupsRestoreLocale() - Restore the original locale...
 *   _cupsSaveLocale()    - Set the locale and save a copy of the old locale...
 *   appleLangDefault()   - Get the default locale string.
 *   cups_cache_lookup()  - Lookup a language in the cache...
 */

/*
 * Include necessary headers...
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#ifdef HAVE_LANGINFO_H
#  include <langinfo.h>
#endif /* HAVE_LANGINFO_H */
#ifdef WIN32
#  include <io.h>
#else
#  include <unistd.h>
#endif /* WIN32 */
#include "string.h"
#include "language.h"
#include "globals.h"
#include "debug.h"


/*
 * Local functions...
 */

#ifdef __APPLE__
#  include <CoreFoundation/CoreFoundation.h>
static const char	*appleLangDefault(void);
#endif /* __APPLE__ */

static cups_lang_t	*cups_cache_lookup(const char *name,
			                   cups_encoding_t encoding);


/*
 * Local globals...
 */

static const char	lang_blank[] = "";
					/* Blank constant string */
static const char * const lang_encodings[] =
			{		/* Encoding strings */
			  "us-ascii",
			  "iso-8859-1",
			  "iso-8859-2",
			  "iso-8859-3",
			  "iso-8859-4",
			  "iso-8859-5",
			  "iso-8859-6",
			  "iso-8859-7",
			  "iso-8859-8",
			  "iso-8859-9",
			  "iso-8859-10",
			  "utf-8",
			  "iso-8859-13",
			  "iso-8859-14",
			  "iso-8859-15",
			  "windows-874",
			  "windows-1250",
			  "windows-1251",
			  "windows-1252",
			  "windows-1253",
			  "windows-1254",
			  "windows-1255",
			  "windows-1256",
			  "windows-1257",
			  "windows-1258",
			  "koi8-r",
			  "koi8-u"
			};
static const char * const lang_default[] =
			{		/* Default POSIX locale */
#include "cups_C.h"
			  NULL
			};


/*
 * 'cupsLangEncoding()' - Return the character encoding (us-ascii, etc.)
 *                        for the given language.
 */

char *					/* O - Character encoding */
cupsLangEncoding(cups_lang_t *lang)	/* I - Language data */
{
  if (lang == NULL)
    return ((char*)lang_encodings[0]);
  else
    return ((char*)lang_encodings[lang->encoding]);
}


/*
 * 'cupsLangFlush()' - Flush all language data out of the cache.
 */

void
cupsLangFlush(void)
{
  _cupsLangFlush(_cups_globals());
  return;
}

/*
 * '_cupsLangFlush()' - Flush all language data out of the cache.
 *
 *  This version is used by the thread destructor which no longer
 *  has access to thread specific global data.
 */

void
_cupsLangFlush(cups_globals_t *cg)
{
  int		i;			/* Looping var */
  cups_lang_t	*lang,			/* Current language */
		*next;			/* Next language */


 /*
  * Free all languages in the cache...
  */

  for (lang = cg->lang_cache; lang != NULL; lang = next)
  {
   /*
    * Free all messages...
    */

    for (i = 0; i < CUPS_MSG_MAX; i ++)
      if (lang->messages[i] != NULL && lang->messages[i] != lang_blank)
        free(lang->messages[i]);

   /*
    * Then free the language structure itself...
    */

    next = lang->next;
    free(lang);
  }

  cg->lang_cache = NULL;
}


/*
 * 'cupsLangFree()' - Free language data.
 *
 * This does not actually free anything; use cupsLangFlush() for that.
 */

void
cupsLangFree(cups_lang_t *lang)	/* I - Language to free */
{
  if (lang != NULL && lang->used > 0)
    lang->used --;
}


/*
 * 'cupsLangGet()' - Get a language.
 */

cups_lang_t *				/* O - Language data */
cupsLangGet(const char *language)	/* I - Language or locale */
{
  int			i, count;	/* Looping vars */
  char			locale[255],	/* Copy of locale name */
			langname[16],	/* Requested language name */
			country[16],	/* Country code */
			charset[16],	/* Character set */
#ifdef CODESET
			*csptr,		/* Pointer to CODESET string */
#endif /* CODESET */
			*ptr,		/* Pointer into language/charset */
			real[48],	/* Real language name */
			filename[1024],	/* Filename for language locale file */
			*localedir;	/* Directory for locale files */
  cups_encoding_t	encoding;	/* Encoding to use */
  FILE			*fp;		/* Language locale file pointer */
  char			line[1024];	/* Line from file */
  cups_msg_t		msg;		/* Message number */
  char			*text;		/* Message text */
  cups_lang_t		*lang;		/* Current language... */
  char			*oldlocale;	/* Old locale name */
  cups_globals_t	*cg = _cups_globals();
  					/* Pointer to library globals */
  static const char * const locale_encodings[] =
			{		/* Locale charset names */
			  "ASCII",
			  "ISO88591",
			  "ISO88592",
			  "ISO88593",
			  "ISO88594",
			  "ISO88595",
			  "ISO88596",
			  "ISO88597",
			  "ISO88598",
			  "ISO88599",
			  "ISO885910",
			  "UTF8",
			  "ISO885913",
			  "ISO885914",
			  "ISO885915",
			  "CP874",
			  "CP1250",
			  "CP1251",
			  "CP1252",
			  "CP1253",
			  "CP1254",
			  "CP1255",
			  "CP1256",
			  "CP1257",
			  "CP1258",
			  "KOI8R",
			  "KOI8U"
			};


  DEBUG_printf(("cupsLangGet(language=\"%s\")\n", language ? language : "(null)"));

#ifdef __APPLE__
 /*
  * Apple's setlocale doesn't give us the user's localization 
  * preference so we have to look it up this way...
  */

  if (language == NULL)
    language = appleLangDefault();
#else
  if (language == NULL)
  {
   /*
    * First see if the locale has been set; if it is still "C" or
    * "POSIX", set the locale to the default...
    */

#  ifdef LC_MESSAGES
    ptr = setlocale(LC_MESSAGES, NULL);
#  else
    ptr = setlocale(LC_ALL, NULL);
#  endif /* LC_MESSAGES */

    DEBUG_printf(("cupsLangGet: current locale is \"%s\"\n",
                  ptr ? ptr : "(null)"));

    if (!ptr || !strcmp(ptr, "C") || !strcmp(ptr, "POSIX"))
#  ifdef LC_MESSAGES
    {
      ptr = setlocale(LC_MESSAGES, "");
      setlocale(LC_CTYPE, "");
    }
#  else
      ptr = setlocale(LC_ALL, "");
#  endif /* LC_MESSAGES */

    if (ptr)
    {
      strlcpy(locale, ptr, sizeof(locale));
      language = locale;

      DEBUG_printf(("cupsLangGet: new language value is \"%s\"\n",
                    language ? language : "(null)"));
    }
  }
#endif /* __APPLE__ */

 /*
  * If "language" is NULL at this point, then chances are we are using
  * a language that is not installed for the base OS.
  */

  if (!language)
  {
   /*
    * Switch to the value of the "LANG" environment variable, and if
    * that is NULL as well, use "C".
    */

    if ((language = getenv("LANG")) == NULL)
      language = "C";
  }

 /*
  * Set the charset to "unknown"...
  */

  charset[0] = '\0';

#ifdef CODESET
 /*
  * On systems that support the nl_langinfo(CODESET) call, use
  * this value as the character set...
  */

  if ((csptr = nl_langinfo(CODESET)) != NULL)
  {
   /*
    * Copy all of the letters and numbers in the CODESET string...
    */

    for (ptr = charset; *csptr; csptr ++)
      if (isalnum(*csptr & 255) && ptr < (charset + sizeof(charset) - 1))
        *ptr++ = *csptr;

    *ptr = '\0';

    DEBUG_printf(("cupsLangGet: charset set to \"%s\" via nl_langinfo(CODESET)...\n",
                  charset));
  }
#endif /* CODESET */

 /*
  * Set the locale back to POSIX while we do string ops, since
  * apparently some buggy C libraries break ctype() for non-I18N
  * chars...
  */

#if defined(__APPLE__)
  /* The ctype bug isn't in Apple's libc */
  (void)locale;			/* anti-compiler-warning-code */
  (void)oldlocale;		/* anti-compiler-warning-code */
#elif !defined(LC_CTYPE)
  oldlocale = _cupsSaveLocale(LC_ALL, "C");
#else
  oldlocale = _cupsSaveLocale(LC_CTYPE, "C");
#endif /* __APPLE__ */

 /*
  * Parse the language string passed in to a locale string. "C" is the
  * standard POSIX locale and is copied unchanged.  Otherwise the
  * language string is converted from ll-cc[.charset] (language-country)
  * to ll_CC[.CHARSET] to match the file naming convention used by all
  * POSIX-compliant operating systems.  Invalid language names are mapped
  * to the POSIX locale.
  */

  country[0] = '\0';

  if (language == NULL || !language[0] ||
      strcmp(language, "POSIX") == 0)
    strcpy(langname, "C");
  else
  {
   /*
    * Copy the parts of the locale string over safely...
    */

    for (ptr = langname; *language; language ++)
      if (*language == '_' || *language == '-' || *language == '.')
	break;
      else if (ptr < (langname + sizeof(langname) - 1))
        *ptr++ = tolower(*language & 255);

    *ptr = '\0';

    if (*language == '_' || *language == '-')
    {
     /*
      * Copy the country code...
      */

      for (language ++, ptr = country; *language; language ++)
	if (*language == '.')
	  break;
	else if (ptr < (country + sizeof(country) - 1))
          *ptr++ = toupper(*language & 255);

      *ptr = '\0';
    }

    if (*language == '.' && !charset[0])
    {
     /*
      * Copy the encoding...
      */

      for (language ++, ptr = charset; *language; language ++)
        if (isalnum(*language & 255) && ptr < (charset + sizeof(charset) - 1))
          *ptr++ = toupper(*language & 255);

      *ptr = '\0';
    }

   /*
    * Force a POSIX locale for an invalid language name...
    */

    if (strlen(langname) != 2)
    {
      strcpy(langname, "C");
      country[0] = '\0';
      charset[0] = '\0';
    }
  }

 /*
  * Restore the locale...
  */

#if defined(__APPLE__)
  /* The ctype bug isn't in Apple's libc */
#elif !defined(LC_CTYPE)
  _cupsRestoreLocale(LC_ALL, oldlocale);
#else
  _cupsRestoreLocale(LC_CTYPE, oldlocale);
#endif /* __APPLE__ */

  DEBUG_printf(("cupsLangGet: langname=\"%s\", country=\"%s\", charset=\"%s\"\n",
                langname, country, charset));

 /*
  * Figure out the desired encoding...
  */

  encoding = CUPS_AUTO_ENCODING;

  if (charset[0])
  {
    for (i = 0; i < (int)(sizeof(locale_encodings) / sizeof(locale_encodings[0])); i ++)
      if (!strcasecmp(charset, locale_encodings[i]))
      {
	encoding = (cups_encoding_t)i;
	break;
      }
  }

  DEBUG_printf(("cupsLangGet: encoding=%d(%s)\n", encoding,
                encoding == CUPS_AUTO_ENCODING ? "auto" :
		    lang_encodings[encoding]));

 /*
  * Now find the message catalog for this locale...
  */

  if ((localedir = getenv("LOCALEDIR")) == NULL)
    localedir = CUPS_LOCALEDIR;

 /*
  * See if we already have this language/country loaded...
  */

  snprintf(real, sizeof(real), "%s_%s", langname, country);

  if ((lang = cups_cache_lookup(real, encoding)) != NULL)
    return (lang);

  snprintf(filename, sizeof(filename), "%s/%s/cups_%s", localedir, real, real);

  if (!country[0] || access(filename, 0))
  {
   /*
    * Country localization not available, look for generic localization...
    */

    if ((lang = cups_cache_lookup(langname, encoding)) != NULL)
      return (lang);

    snprintf(filename, sizeof(filename), "%s/%s/cups_%s", localedir,
             langname, langname);

    if (access(filename, 0))
    {
     /*
      * No generic localization, so use POSIX...
      */

      strcpy(real, "C");
      snprintf(filename, sizeof(filename), "%s/C/cups_C", localedir);
    }
    else
      strcpy(real, langname);
  }

 /*
  * Open the messages file; the first line contains the default
  * language encoding (us-ascii, iso-8859-1, etc.), and the rest are
  * messages consisting of:
  *
  *    #### SP message text
  *
  * or:
  *
  *    message text
  *
  * If the line starts with a number, then message processing picks up
  * where the number indicates.  Otherwise the last message number is
  * incremented.
  *
  * All leading whitespace is deleted.
  */

  if (strcmp(real, "C"))
    fp = fopen(filename, "r");
  else
    fp = NULL;

  if (fp == NULL)
    strlcpy(line, lang_default[0], sizeof(line));
  else if (fgets(line, sizeof(line), fp) == NULL)
  {
   /*
    * Can't read encoding!
    */

    fclose(fp);
    return (NULL);
  }

  i = strlen(line) - 1;
  if (line[i] == '\n')
    line[i] = '\0';	/* Strip LF */

 /*
  * See if there is a free language available; if so, use that
  * record...
  */

  for (lang = cg->lang_cache; lang != NULL; lang = lang->next)
    if (lang->used == 0)
      break;

  if (lang == NULL)
  {
   /*
    * Allocate memory for the language and add it to the cache.
    */

    if ((lang = calloc(sizeof(cups_lang_t), 1)) == NULL)
    {
      fclose(fp);
      return (NULL);
    }

    lang->next = cg->lang_cache;
    cg->lang_cache = lang;
  }

 /*
  * Free all old strings as needed...
  */

  for (i = 0; i < CUPS_MSG_MAX; i ++)
  {
    if (lang->messages[i] != NULL && lang->messages[i] != lang_blank)
      free(lang->messages[i]);

    lang->messages[i] = (char*)lang_blank;
  }

 /*
  * Then assign the language and encoding fields...
  */

  lang->used ++;
  strlcpy(lang->language, real, sizeof(lang->language));

  if (encoding != CUPS_AUTO_ENCODING)
    lang->encoding = encoding;
  else
  {
    lang->encoding = CUPS_US_ASCII;

    for (i = 0; i < (sizeof(lang_encodings) / sizeof(lang_encodings[0])); i ++)
      if (strcmp(lang_encodings[i], line) == 0)
      {
	lang->encoding = (cups_encoding_t)i;
	break;
      }
  }

 /*
  * Read the strings from the file...
  */

  msg   = (cups_msg_t)-1;
  count = 1;

  for (;;)
  {
   /*
    * Read a line from memory or from a file...
    */

    if (fp == NULL)
    {
      if (lang_default[count] == NULL)
        break;

      strlcpy(line, lang_default[count], sizeof(line));
    }
    else if (fgets(line, sizeof(line), fp) == NULL)
      break;

    count ++;

   /*
    * Ignore blank lines...
    */

    i = strlen(line) - 1;
    if (line[i] == '\n')
      line[i] = '\0';	/* Strip LF */

    if (line[0] == '\0')
      continue;

   /*
    * Grab the message number and text...
    */

    if (isdigit(line[0] & 255))
      msg = (cups_msg_t)atoi(line);
    else
      msg ++;

    if (msg < 0 || msg >= CUPS_MSG_MAX)
      continue;

    text = line;
    while (isdigit(*text & 255))
      text ++;
    while (isspace(*text & 255))
      text ++;
    
    lang->messages[msg] = strdup(text);
  }

 /*
  * Close the file and return...
  */

  if (fp != NULL)
    fclose(fp);

  return (lang);
}


/*
 * '_cupsRestoreLocale()' - Restore the original locale...
 */

void
_cupsRestoreLocale(int  category,	/* I - Category */
                   char *oldlocale)	/* I - Old locale or NULL */
{
  DEBUG_printf(("_cupsRestoreLocale(category=%d, oldlocale=\"%s\")\n",
                category, oldlocale));

  if (oldlocale)
  {
   /*
    * Reset the locale and free the locale string...
    */

    setlocale(category, oldlocale);
    free(oldlocale);
  }
}


/*
 * '_cupsSaveLocale()' - Set the locale and save a copy of the old locale...
 */

char *					/* O - Old locale or NULL */
_cupsSaveLocale(int        category,	/* I - Category */
                const char *locale)	/* I - New locale or NULL */
{
  char	*oldlocale;			/* Old locale */


  DEBUG_printf(("_cupsSaveLocale(category=%d, locale=\"%s\")\n",
                category, locale));

 /*
  * Get the old locale and copy it...
  */

  if ((oldlocale = setlocale(category, NULL)) != NULL)
    oldlocale = strdup(oldlocale);

  DEBUG_printf(("    oldlocale=\"%s\"\n", oldlocale ? oldlocale : "(null)"));

 /*
  * Set the new locale...
  */

  setlocale(category, locale);

 /*
  * Return a copy of the old locale...
  */

  return (oldlocale);
}


#ifdef __APPLE__
/*
 * Code & data to translate OSX's language names to their ISO 639-1 locale.
 *
 * The first version uses the new CoreFoundation API added in 10.3 (Panther),
 * the second is for 10.2 (Jaguar).
 */

#  ifdef HAVE_CF_LOCALE_ID
/*
 * 'appleLangDefault()' - Get the default locale string.
 */

static const char *			/* O - Locale string */
appleLangDefault(void)
{
  CFPropertyListRef 	localizationList;
					/* List of localization data */
  CFStringRef		languageName;	/* Current name */
  CFStringRef		localeName;	/* Canonical from of name */
  cups_globals_t	*cg = _cups_globals();
  					/* Pointer to library globals */


 /*
  * Only do the lookup and translation the first time.
  */

  if (!cg->apple_language[0])
  {
    localizationList =
        CFPreferencesCopyAppValue(CFSTR("AppleLanguages"),
                                  kCFPreferencesCurrentApplication);

    if (localizationList != NULL)
    {
      if (CFGetTypeID(localizationList) == CFArrayGetTypeID() &&
	  CFArrayGetCount(localizationList) > 0)
      {
        languageName = CFArrayGetValueAtIndex(localizationList, 0);

        if (languageName != NULL &&
            CFGetTypeID(languageName) == CFStringGetTypeID())
        {
	  localeName = CFLocaleCreateCanonicalLocaleIdentifierFromString(
	                   kCFAllocatorDefault, languageName);

	  if (localeName != NULL)
	  {
	    CFStringGetCString(localeName, cg->apple_language, sizeof(cg->apple_language),
			       kCFStringEncodingASCII);
	    CFRelease(localeName);

	    if (!strcmp(cg->apple_language, "en"))
	      strlcpy(cg->apple_language, "en_US.UTF-8", sizeof(cg->apple_language));
	    else if (strchr(cg->apple_language, '.') == NULL)
	      strlcat(cg->apple_language, ".UTF-8", sizeof(cg->apple_language));
	  }
        }
      }

      CFRelease(localizationList);
    }
  
   /*
    * If we didn't find the language, default to en_US...
    */

    if (!cg->apple_language[0])
      strlcpy(cg->apple_language, "en_US.UTF-8", sizeof(cg->apple_language));
  }

 /*
  * Return the cached locale...
  */

  return (cg->apple_language);
}
#  else
/*
 * Code & data to translate OSX 10.2's language names to their ISO 639-1
 * locale.
 */

typedef struct
{
  const char * const name;		/* Language name */
  const char * const locale;		/* Locale name */
} apple_name_locale_t;

static const apple_name_locale_t apple_name_locale[] =
{
  { "English"     , "en_US.UTF-8" },	{ "French"     , "fr.UTF-8" },
  { "German"      , "de.UTF-8" },	{ "Italian"    , "it.UTF-8" },  
  { "Dutch"       , "nl.UTF-8" },	{ "Swedish"    , "sv.UTF-8" },
  { "Spanish"     , "es.UTF-8" },	{ "Danish"     , "da.UTF-8" },  
  { "Portuguese"  , "pt.UTF-8" },	{ "Norwegian"  , "no.UTF-8" },
  { "Hebrew"      , "he.UTF-8" },	{ "Japanese"   , "ja.UTF-8" },  
  { "Arabic"      , "ar.UTF-8" },	{ "Finnish"    , "fi.UTF-8" },
  { "Greek"       , "el.UTF-8" },	{ "Icelandic"  , "is.UTF-8" },  
  { "Maltese"     , "mt.UTF-8" },	{ "Turkish"    , "tr.UTF-8" },
  { "Croatian"    , "hr.UTF-8" },	{ "Chinese"    , "zh.UTF-8" },  
  { "Urdu"        , "ur.UTF-8" },	{ "Hindi"      , "hi.UTF-8" },
  { "Thai"        , "th.UTF-8" },	{ "Korean"     , "ko.UTF-8" },  
  { "Lithuanian"  , "lt.UTF-8" },	{ "Polish"     , "pl.UTF-8" },
  { "Hungarian"   , "hu.UTF-8" },	{ "Estonian"   , "et.UTF-8" },  
  { "Latvian"     , "lv.UTF-8" },	{ "Sami"       , "se.UTF-8" },
  { "Faroese"     , "fo.UTF-8" },	{ "Farsi"      , "fa.UTF-8" },  
  { "Russian"     , "ru.UTF-8" },	{ "Chinese"    , "zh.UTF-8" },
  { "Dutch"       , "nl.UTF-8" },	{ "Irish"      , "ga.UTF-8" },  
  { "Albanian"    , "sq.UTF-8" },	{ "Romanian"   , "ro.UTF-8" },
  { "Czech"       , "cs.UTF-8" },	{ "Slovak"     , "sk.UTF-8" },  
  { "Slovenian"   , "sl.UTF-8" },	{ "Yiddish"    , "yi.UTF-8" },
  { "Serbian"     , "sr.UTF-8" },	{ "Macedonian" , "mk.UTF-8" },  
  { "Bulgarian"   , "bg.UTF-8" },	{ "Ukrainian"  , "uk.UTF-8" },
  { "Byelorussian", "be.UTF-8" },	{ "Uzbek"      , "uz.UTF-8" },  
  { "Kazakh"      , "kk.UTF-8" },	{ "Azerbaijani", "az.UTF-8" },
  { "Azerbaijani" , "az.UTF-8" },	{ "Armenian"   , "hy.UTF-8" },  
  { "Georgian"    , "ka.UTF-8" },	{ "Moldavian"  , "mo.UTF-8" },
  { "Kirghiz"     , "ky.UTF-8" },	{ "Tajiki"     , "tg.UTF-8" },  
  { "Turkmen"     , "tk.UTF-8" },	{ "Mongolian"  , "mn.UTF-8" },
  { "Mongolian"   , "mn.UTF-8" },	{ "Pashto"     , "ps.UTF-8" },  
  { "Kurdish"     , "ku.UTF-8" },	{ "Kashmiri"   , "ks.UTF-8" },
  { "Sindhi"      , "sd.UTF-8" },	{ "Tibetan"    , "bo.UTF-8" },  
  { "Nepali"      , "ne.UTF-8" },	{ "Sanskrit"   , "sa.UTF-8" },
  { "Marathi"     , "mr.UTF-8" },	{ "Bengali"    , "bn.UTF-8" },  
  { "Assamese"    , "as.UTF-8" },	{ "Gujarati"   , "gu.UTF-8" },
  { "Punjabi"     , "pa.UTF-8" },	{ "Oriya"      , "or.UTF-8" },  
  { "Malayalam"   , "ml.UTF-8" },	{ "Kannada"    , "kn.UTF-8" },
  { "Tamil"       , "ta.UTF-8" },	{ "Telugu"     , "te.UTF-8" },  
  { "Sinhalese"   , "si.UTF-8" },	{ "Burmese"    , "my.UTF-8" },
  { "Khmer"       , "km.UTF-8" },	{ "Lao"        , "lo.UTF-8" },  
  { "Vietnamese"  , "vi.UTF-8" },	{ "Indonesian" , "id.UTF-8" },
  { "Tagalog"     , "tl.UTF-8" },	{ "Malay"      , "ms.UTF-8" },  
  { "Malay"       , "ms.UTF-8" },	{ "Amharic"    , "am.UTF-8" },
  { "Tigrinya"    , "ti.UTF-8" },	{ "Oromo"      , "om.UTF-8" },  
  { "Somali"      , "so.UTF-8" },	{ "Swahili"    , "sw.UTF-8" },
  { "Kinyarwanda" , "rw.UTF-8" },	{ "Rundi"      , "rn.UTF-8" },  
  { "Nyanja"      , "" },		{ "Malagasy"   , "mg.UTF-8" },
  { "Esperanto"   , "eo.UTF-8" },	{ "Welsh"      , "cy.UTF-8" },  
  { "Basque"      , "eu.UTF-8" },	{ "Catalan"    , "ca.UTF-8" },
  { "Latin"       , "la.UTF-8" },	{ "Quechua"    , "qu.UTF-8" },  
  { "Guarani"     , "gn.UTF-8" },	{ "Aymara"     , "ay.UTF-8" },
  { "Tatar"       , "tt.UTF-8" },	{ "Uighur"     , "ug.UTF-8" },  
  { "Dzongkha"    , "dz.UTF-8" },	{ "Javanese"   , "jv.UTF-8" },
  { "Sundanese"   , "su.UTF-8" },	{ "Galician"   , "gl.UTF-8" },  
  { "Afrikaans"   , "af.UTF-8" },	{ "Breton"     , "br.UTF-8" },
  { "Inuktitut"   , "iu.UTF-8" },	{ "Scottish"   , "gd.UTF-8" },  
  { "Manx"        , "gv.UTF-8" },	{ "Irish"      , "ga.UTF-8" },
  { "Tongan"      , "to.UTF-8" },	{ "Greek"      , "el.UTF-8" },  
  { "Greenlandic" , "kl.UTF-8" },	{ "Azerbaijani", "az.UTF-8" }
};


/*
 * 'appleLangDefault()' - Get the default locale string.
 */

static const char *			/* O - Locale string */
appleLangDefault(void)
{
  int			i;		/* Looping var */
  CFPropertyListRef 	localizationList;
					/* List of localization data */
  CFStringRef		localizationName;
					/* Current name */
  char			buff[256];	/* Temporary buffer */
  cups_globals_t	*cg = _cups_globals();
  					/* Pointer to library globals */


 /*
  * Only do the lookup and translation the first time.
  */

  if (cg->apple_language == NULL)
  {
    localizationList =
        CFPreferencesCopyAppValue(CFSTR("AppleLanguages"),
                                  kCFPreferencesCurrentApplication);

    if (localizationList != NULL)
    {
      if (CFGetTypeID(localizationList) == CFArrayGetTypeID() &&
	  CFArrayGetCount(localizationList) > 0)
      {
	localizationName = CFArrayGetValueAtIndex(localizationList, 0);

	if (localizationName != NULL &&
            CFGetTypeID(localizationName) == CFStringGetTypeID())
	{
	  CFIndex length = CFStringGetLength(localizationName);

	  if (length <= sizeof(buff) &&
	      CFStringGetCString(localizationName, buff, sizeof(buff),
	                         kCFStringEncodingASCII))
	  {
	    buff[sizeof(buff) - 1] = '\0';

	    for (i = 0;
		 i < sizeof(apple_name_locale) / sizeof(apple_name_locale[0]);
		 i++)
	    {
	      if (strcasecmp(buff, apple_name_locale[i].name) == 0)
	      {
		cg->apple_language = apple_name_locale[i].locale;
		break;
	      }
	    }
	  }
	}
      }

      CFRelease(localizationList);
    }
  
   /*
    * If we didn't find the language, default to en_US...
    */

    if (cg->apple_language == NULL)
      cg->apple_language = apple_name_locale[0].locale;
  }

 /*
  * Return the cached locale...
  */

  return (cg->apple_language);
}
#  endif /* HAVE_CF_LOCALE_ID */
#endif /* __APPLE__ */


/*
 * 'cups_cache_lookup()' - Lookup a language in the cache...
 */

static cups_lang_t *			/* O - Language data or NULL */
cups_cache_lookup(const char      *name,/* I - Name of locale */
                  cups_encoding_t encoding)
					/* I - Encoding of locale */
{
  cups_lang_t	*lang;			/* Current language */


  DEBUG_printf(("cups_cache_lookup(name=\"%s\", encoding=%d(%s))\n", name,
                encoding, encoding == CUPS_AUTO_ENCODING ? "auto" :
		              lang_encodings[encoding]));

 /*
  * Loop through the cache and return a match if found...
  */

  for (lang = _cups_globals()->lang_cache; lang != NULL; lang = lang->next)
    if (!strcmp(lang->language, name) &&
        (encoding == CUPS_AUTO_ENCODING || encoding == lang->encoding))
    {
      lang->used ++;

      DEBUG_puts("cups_cache_lookup: returning match!");

      return (lang);
    }

  DEBUG_puts("cups_cache_lookup: returning NULL!");

  return (NULL);
}


/*
 * End of "$Id: language.c,v 1.16.2.1 2006/02/08 22:04:46 jlovell Exp $".
 */
