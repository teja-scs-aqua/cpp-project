/* Metrowerks Standard Library
 * Copyright � 1995-2002 Metrowerks Corporation.  All rights reserved.
 *
 * $Date: 2003/05/07 14:14:38 $
 * $Revision: 1.22.2.6.2.9 $
 */

/*
 *	Routines
 *	--------
 *		setlocale
 *		localeconv
 */

#include <locale.h>
#include <limits.h>
#include <string.h>
#include <stddef.h>
#include <cwctype>	
#include <cstdlib>	
#include <mbstring.h>
#include <ctype_api.h>
#include <wctype_api.h>
#include <locale_api.h>

#if !_MSL_C_LOCALE_ONLY

#if (_MSL_THREADSAFE && (__dest_os == __win32_os || __dest_os == __wince_os))
	#include <ThreadLocalData.h>
#endif

struct lconv  __lconv =
{
	"."			 /* decimal_point		*/,
	""			 /* thousands_sep		*/,
	""			 /* grouping			*/,
	""			 /* mon_decimal_point	*/,
	""			 /* mon_thousands_sep	*/,
	""			 /* mon_grouping		*/,
	""			 /* positive_sign		*/,
	""			 /* negative_sign		*/,
	""			 /* currency_symbol		*/,
	CHAR_MAX	 /* frac_digits			*/,
	CHAR_MAX	 /* p_cs_precedes		*/,
	CHAR_MAX	 /* n_cs_precedes		*/,
	CHAR_MAX	 /* p_sep_by_space		*/,
	CHAR_MAX	 /* n_sep_by_space		*/,
	CHAR_MAX	 /* p_sign_posn			*/,
	CHAR_MAX	 /* n_sign_posn			*/,
	""			 /* int_curr_symbol		*/,
	CHAR_MAX	 /* int_frac_digits		*/,
	CHAR_MAX 	 /* int_p_cs_precedes	*/, 
	CHAR_MAX 	 /* int_n_cs_precedes	*/,
	CHAR_MAX 	 /* int_p_sep_by_space	*/,
	CHAR_MAX 	 /* int_n_sep_by_space	*/, 
	CHAR_MAX 	 /* int_p_sign_posn		*/,
	CHAR_MAX 	 /* int_n_sign_posn		*/
};

#endif /*!_MSL_C_LOCALE_ONLY */

#if !_MSL_C_LOCALE_ONLY && !(defined(__NO_WIDE_CHAR))
struct _loc_ctype_cmpt _loc_ctyp_C =
{
	"C",
	&__ctype_mapC[0],
	&__upper_mapC[0],
	&__lower_mapC[0],
	&__wctype_mapC[0],
	&__wupper_mapC[0],
	&__wlower_mapC[0],
	__mbtowc_noconv,
	__wctomb_noconv
};
#elif _MSL_C_LOCALE_ONLY && !(defined(__NO_WIDE_CHAR))
struct _loc_ctype_cmpt _loc_ctyp_C =
{
	__mbtowc_noconv,
	__wctomb_noconv
};
#endif

#if !_MSL_C_LOCALE_ONLY
struct _loc_ctype_cmpt _loc_ctyp_I =
{
	"",												/*- mm 020108 -*/
	&__msl_ctype_map[0],
	&__upper_map[0],
	&__lower_map[0],
#ifndef __NO_WIDE_CHAR								/*- mm 020404 -*/
	&__msl_wctype_map[0],
	&__wupper_map[0],
	&__wlower_map[0],
	__mbtowc_noconv,
	__wctomb_noconv
#endif												/*- mm 020404 -*/
};

struct _loc_ctype_cmpt _loc_ctyp_C_UTF_8 =
{
	"C-UTF-8",										/*- mm 020108 -*/
	&__ctype_mapC[0],
	&__upper_mapC[0],
	&__lower_mapC[0],
#ifndef __NO_WIDE_CHAR
	&__wctype_mapC[0],
	&__wupper_mapC[0],
	&__wlower_mapC[0],
	__utf8_to_unicode,
	__unicode_to_UTF8
#endif
};

struct _loc_coll_cmpt _loc_coll_C =
{
	"C",   /* component name     */
	NULL,  /* char_coll_seq_ptr  */
	NULL   /* wchar_coll_seq_ptr */
};

struct _loc_mon_cmpt _loc_mon_C =
{
	"C",
	"",  		/* mon_decimal_point	*/
	"",  		/* mon_thousands_sep	*/ 
	"",  		/* mon_grouping			*/
	"",  		/* positive_sign		*/
	"",  		/* negative_sign		*/ 
	"",			/* currency_symbol		*/
	CHAR_MAX,	/* frac_digits			*/ 
	CHAR_MAX,	/* p_cs_precedes		*/ 
	CHAR_MAX,	/* n_cs_precedes		*/  
	CHAR_MAX,	/* p_sep_by_space		*/  
	CHAR_MAX,	/* n_sep_by_space		*/  
	CHAR_MAX,	/* p_sign_posn			*/  
	CHAR_MAX,	/* n_sign_posn			*/  
	"",			/* int_curr_symbol		*/ 
	CHAR_MAX,	/* int_frac_digits		*/ 
	CHAR_MAX,	/* int_p_cs_precedes	*/  
	CHAR_MAX,	/* int_n_cs_precedes	*/ 
	CHAR_MAX,	/* int_p_sep_by_space	*/ 
	CHAR_MAX,	/* int_n_sep_by_space	*/ 
	CHAR_MAX,	/* int_p_sign_posn		*/ 
	CHAR_MAX	/* int_n_sign_posn		*/ 
};

struct _loc_num_cmpt _loc_num_C =
{
	"C",
	".", 		/* decimal_point	*/
	"",			/* thousands_sep	*/
	""			/* grouping			*/
};
#endif /*!_MSL_C_LOCALE_ONLY */

struct _loc_time_cmpt  _loc_tim_C =
{
#if !_MSL_C_LOCALE_ONLY
	"C",
#endif /* _MSL_C_LOCALE_ONLY */
	"AM|PM",						/*  am_pm			*/ /*- mm 021204 -*/
	"%a %b %e %T %Y|%I:%M:%S %p|%m/%d/%y|%T",       	/*  datetime_fmts in the order %c|%r|%x|%X */
	"Sun|Sunday|Mon|Monday|Tue|Tuesday|Wed|Wednesday"
					"|Thu|Thursday|Fri|Friday|Sat|Saturday",	/*  day_names 		*/
	"Jan|January|Feb|February|Mar|March"
       "|Apr|April|May|May|Jun|June"
       "|Jul|July|Aug|August|Sep|September"
       "|Oct|October|Nov|November|Dec|December",				/*  month_names		*/
    ""															/*  time zone       */ /*- mm 020710 -*/
};

#if _MSL_C_LOCALE_ONLY

struct __locale  _current_locale =
{
	&_loc_tim_C,			/* time_component		*/
#ifndef __NO_WIDE_CHAR
	&_loc_ctyp_C			/* ctype_component		*/
#endif
};

#else

struct __locale  _current_locale =								/*- mm 011205 -*/
{
	NULL,                   /* next_locale 			*/
#if (_MSL_DEFAULT_LOCALE == _MSL_LOCALE_C)						/*- mm 020212 -*/
	"C",					/* locale_name			*/
#elif (_MSL_DEFAULT_LOCALE == _MSL_LOCALE_CUTF8)				/*- mm 020118 -*/ /*- mm 020212 -*/
	"C|C-UTF-8|C|C|C",		/* locale_name    		*/			/*- mm 020212 -*/
#else															/*- mm 020212 -*/
	"C||C|C|C",				/* locale_name    		*/			/*- mm 020118 -*/
#endif															/*- mm 020118 -*/
	&_loc_coll_C, 			/* collate_component	*/
#if (_MSL_DEFAULT_LOCALE == _MSL_LOCALE_C)						/*- mm 020212 -*/
	&_loc_ctyp_C,			/* ctype_component		*/
#elif (_MSL_DEFAULT_LOCALE == _MSL_LOCALE_CUTF8)				/*- mm 020118 -*/ /*- mm 020212 -*/
	&_loc_ctyp_C_UTF_8,		/* ctype_component		*/			/*- mm 020212 -*/
#else															/*- mm 020212 -*/
	&_loc_ctyp_I,			/* ctype_component		*/			/*- mm 020212 -*/
#endif															/*- mm 020118 -*/
	&_loc_mon_C, 			/* monetary_component	*/
	&_loc_num_C, 			/* numeric_component	*/
	&_loc_tim_C 			/* time_component		*/
};

#endif /* _MSL_C_LOCALE_ONLY */

#if !_MSL_C_LOCALE_ONLY

struct __locale _preset_locales[3] =								/*- mm 011205 -*/
{
	{
		&_preset_locales[1],    /* next_locale 			*/
		"C",					/* locale_name			*/
		&_loc_coll_C, 			/* collate_component	*/
		&_loc_ctyp_C,			/* ctype_component		*/
		&_loc_mon_C, 			/* monetary_component	*/
		&_loc_num_C, 			/* numeric_component	*/
		&_loc_tim_C 			/* time_component		*/
	},
	{
		&_preset_locales[2],   	/* next_locale 			*/
		"",						/* locale_name			*/			/*- mm 020118 -*/
		&_loc_coll_C, 			/* collate_component	*/
		&_loc_ctyp_I,			/* ctype_component		*/
		&_loc_mon_C, 			/* monetary_component	*/
		&_loc_num_C, 			/* numeric_component	*/
		&_loc_tim_C 			/* time_component		*/
	},
	{
		NULL,   				/* next_locale 			*/
		"C-UTF-8",				/* locale_name			*/			/*- mm 020118 -*/
		&_loc_coll_C, 			/* collate_component	*/
		&_loc_ctyp_C_UTF_8,		/* ctype_component		*/
		&_loc_mon_C, 			/* monetary_component	*/
		&_loc_num_C, 			/* numeric_component	*/
		&_loc_tim_C 			/* time_component		*/
	}
};

#if !(_MSL_THREADSAFE && (__dest_os == __win32_os || __dest_os == __wince_os))  /*- mm 010521 -*/
	_MSL_TLS static struct lconv public_lconv;									/*- mm 010503 -*/ /*- cc 011128 -*/
#endif
																				/*- mm 010503 -*/
char * _MSL_CDECL setlocale(int category, const char * locale)
{
/*- begin mm 011130 rewrite -*/
	struct __locale * locptr;													/*- mm 011205 -*/
	int       c_locale_is_composite, index;
	char *	  l_name_start;
	char *    l_name_end;
	char      name_list[_LOCALE_CMPT_COUNT][_LOCALE_NAME_LEN];
	int       cmpt_macro_vals[_LOCALE_CMPT_COUNT] = {LC_COLLATE,  LC_CTYPE, LC_MONETARY, LC_NUMERIC, LC_TIME};
	struct __locale *    current_locale_ptr;										/*- mm 011205 -*/
#if !(_MSL_THREADSAFE && (__dest_os == __win32_os || __dest_os == __wince_os))				/*- mm 010521 -*/
	current_locale_ptr = &_current_locale;
#else																				/*- mm 010503 -*/
	current_locale_ptr = &_GetThreadLocalData(_MSL_TRUE)->thread_locale;	
#endif																				/*- mm 010503 -*/
	
	if ((locale == NULL) || (strcmp(locale, current_locale_ptr->locale_name) == 0))
	{
		locptr = current_locale_ptr;												/*- mm 011130 -*/
		switch(category)
		{
			case LC_ALL:
				return(current_locale_ptr->locale_name);
			case LC_COLLATE:
				return(current_locale_ptr->coll_cmpt_ptr->CmptName);
			case LC_CTYPE:
				return(current_locale_ptr->ctype_cmpt_ptr->CmptName);
			case LC_MONETARY:
				return(current_locale_ptr->mon_cmpt_ptr->CmptName);
			case LC_NUMERIC:
				return(current_locale_ptr->num_cmpt_ptr->CmptName);
			case LC_TIME:
				return(current_locale_ptr->time_cmpt_ptr->CmptName);
			default:
				return(NULL);
		}
	}

	/* split locale name into components */
	l_name_start = (char*)locale;
	for(index = 0; index < 5; index++)
	{
		l_name_end = strchr(l_name_start, '|');
		if (l_name_end == NULL)
		{
			strcpy(name_list[index++], l_name_start);
			break;
		}
		else
		{
			strncpy(name_list[index], l_name_start, (size_t)(l_name_end - l_name_start));
			name_list[index][l_name_end - l_name_start] = '\0';
			l_name_start = l_name_end + 1;
		}
	}
	
	if (index == 1)
		c_locale_is_composite = 0;
	else
		if (index == _LOCALE_CMPT_COUNT)
			c_locale_is_composite = 1;
		else     /* given locale name is not valid */
			return(NULL);
	
	if (!c_locale_is_composite) 
	{
		locptr = &_preset_locales[0];
		while(locptr != NULL)  /* search existing locales for given name */
		{
			if (strcmp(locale, locptr->locale_name) == 0)
				break;
			locptr = locptr->next_locale;
		}
		if (locptr == NULL)
			return(NULL);
		else
		{
			switch(category)
			{
				case LC_ALL:
					strcpy(current_locale_ptr->locale_name, locptr->locale_name);		
					current_locale_ptr->coll_cmpt_ptr = locptr->coll_cmpt_ptr;					
					current_locale_ptr->ctype_cmpt_ptr = locptr->ctype_cmpt_ptr;				
					current_locale_ptr->mon_cmpt_ptr = locptr->mon_cmpt_ptr;					
					current_locale_ptr->num_cmpt_ptr = locptr->num_cmpt_ptr;					
					current_locale_ptr->time_cmpt_ptr = locptr->time_cmpt_ptr;					
					memcpy((void *)&__lconv.mon_decimal_point, (void*)&(locptr->mon_cmpt_ptr)->mon_decimal_point,
													sizeof(struct _loc_mon_cmpt_vals));
					memcpy((void *)&__lconv.decimal_point, (void*)&(locptr->num_cmpt_ptr)->decimal_point,
													sizeof(struct _loc_num_cmpt_vals));
				return(current_locale_ptr->locale_name);
				case LC_COLLATE:
					current_locale_ptr->coll_cmpt_ptr = locptr->coll_cmpt_ptr;
					break;
				case LC_CTYPE:
					current_locale_ptr->ctype_cmpt_ptr = locptr->ctype_cmpt_ptr;
					break;
				case LC_MONETARY:
					current_locale_ptr->mon_cmpt_ptr = locptr->mon_cmpt_ptr;
					memcpy((void *)&__lconv.mon_decimal_point, (void*)&(locptr->mon_cmpt_ptr)->mon_decimal_point,
													sizeof(struct _loc_mon_cmpt_vals));
					break;
				case LC_NUMERIC:
					current_locale_ptr->num_cmpt_ptr = locptr->num_cmpt_ptr;
					memcpy((void *)&__lconv.decimal_point, (void*)&(locptr->num_cmpt_ptr)->decimal_point,
													sizeof(struct _loc_num_cmpt_vals));
					break;
				case LC_TIME:
					current_locale_ptr->time_cmpt_ptr = locptr->time_cmpt_ptr;
					break;
				default:
					return(NULL);
			}
		}
	}
	else
	{
		for (index = 0; index <= _LOCALE_CMPT_COUNT; index++)
			if (cmpt_macro_vals[index] & category)
				setlocale(cmpt_macro_vals[index], name_list[index]);
		strcpy(current_locale_ptr->locale_name, locale);
		return(current_locale_ptr->locale_name);
	}
	
	/* construct new locale name */
	strcpy(current_locale_ptr->locale_name, current_locale_ptr->coll_cmpt_ptr->CmptName);
	if ((strcmp(current_locale_ptr->coll_cmpt_ptr->CmptName, current_locale_ptr->ctype_cmpt_ptr->CmptName) != 0) ||
		 (strcmp(current_locale_ptr->coll_cmpt_ptr->CmptName, current_locale_ptr->mon_cmpt_ptr->CmptName) != 0) ||
	     (strcmp(current_locale_ptr->coll_cmpt_ptr->CmptName, current_locale_ptr->num_cmpt_ptr->CmptName) != 0) ||
		 (strcmp(current_locale_ptr->coll_cmpt_ptr->CmptName, current_locale_ptr->time_cmpt_ptr->CmptName) != 0))
	{
		strcat(current_locale_ptr->locale_name, "|");
		strcat(current_locale_ptr->locale_name, current_locale_ptr->ctype_cmpt_ptr->CmptName);
		strcat(current_locale_ptr->locale_name, "|");
		strcat(current_locale_ptr->locale_name, current_locale_ptr->mon_cmpt_ptr->CmptName);
		strcat(current_locale_ptr->locale_name, "|");
		strcat(current_locale_ptr->locale_name, current_locale_ptr->num_cmpt_ptr->CmptName);
		strcat(current_locale_ptr->locale_name, "|");
		strcat(current_locale_ptr->locale_name, current_locale_ptr->time_cmpt_ptr->CmptName);
	}
	
	return(current_locale_ptr->locale_name);
}
/*- end mm 011130 rewrite -*/


struct lconv * _MSL_CDECL localeconv(void)
{
#if (_MSL_THREADSAFE && (__dest_os == __win32_os || __dest_os == __wince_os))
	_GetThreadLocalData(_MSL_TRUE)->tls_lconv = &__lconv;						
	return(_GetThreadLocalData(_MSL_TRUE)->tls_lconv);							
#else																			
	public_lconv = __lconv;														
	return(&public_lconv);														
#endif																			
}

#endif /* _MSL_C_LOCALE_ONLY */

/* Change record:
 * JFH 950612 First code release.
 * mm  010503 Code for thread local data in localeconv.
 * mm  010507 Reorganized the structure lconv to match C99
 * mm  010521 Added _MWMT wrappers
 * cc  010531 Added _GetThreadLocalData's flag
 * cc  011128 Made __tls _MSL_TLS
 * mm  011130 Additions and changes for implementation of locale
 * cc  011203 Added _MSL_CDECL for new name mangling
 * mm  011205 Changed _LOCALE to __locale
 * mm  020108 Corrected name of implementation locale
 * mm  020118 Corrected some errors in locale implementation
 * JWW 020130 Changed _MWMT to _MSL_THREADSAFE for consistency's sake
 * mm  020212 Added choice of ctype component
 * JWW 020304 Fixed the unnamed locale to be named "" instead of " "
 * JWW 020305 Changed to use new "wider is better" ctype classification tables
 * mm  020404 Added __NO_WIDE_CHAR wrappers to ctype locale components
 * BLC 020924 Fixed implicit conversion warning
 * cc  021001 Added support for _MSL_C_LOCALE_ONLY
 * cc  030327 Under _MSL_C_LOCALE_ONLY added _loc_ctyp_C to _current_locale 
 *            and exposed _loc_ctyp_C
 * cc  040103 Blocked _loc_coll_C with __NO_WIDE_CHAR
 */
