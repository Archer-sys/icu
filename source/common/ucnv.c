/*
   ********************************************************************************
   *                                                                              *
   * COPYRIGHT:                                                                   *
   *   (C) Copyright International Business Machines Corporation, 1998            *
   *   Licensed Material - Program-Property of IBM - All Rights Reserved.         *
   *   US Government Users Restricted Rights - Use, duplication, or disclosure    *
   *   restricted by GSA ADP Schedule Contract with IBM Corp.                     *
   *                                                                              *
   ********************************************************************************
   *
   *
   *  ucnv.c:
   *  Implements APIs for the ICU's codeset conversion library
   *  mostly calls through internal functions created and maintained 
   *  by Bertrand A. Damiba
   *
   * Modification History:
   *
   *   Date        Name        Description
   *   04/04/99    helena      Fixed internal header inclusion.
 */
#include "umutex.h"
#include "ures.h"
#include "uhash.h"
#include "ucmp16.h"
#include "ucmp8.h"
#include "ucnv_bld.h"
#include "ucnv_io.h"
#include "ucnv_err.h"
#include "ucnv_cnv.h"
#include "ucnv_imp.h"
#include "ucnv.h"
#include "cmemory.h"
#include "cstring.h"
#include "ustring.h"
#include "uloc.h"

#define CHUNK_SIZE 5*1024


typedef void (*T_ToUnicodeFunction) (UConverter *,
				     UChar **,
				     const UChar *,
				     const char **,
				     const char *,
				     int32_t* offsets,
				     bool_t,
				     UErrorCode *);

typedef void (*T_FromUnicodeFunction) (UConverter *,
				       char **,
				       const char *,
				       const UChar **,
				       const UChar *,
				       int32_t* offsets,
				       bool_t,
				       UErrorCode *);

typedef UChar (*T_GetNextUCharFunction) (UConverter *,
					 const char **,
					 const char *,
					 UErrorCode *);

static T_ToUnicodeFunction TO_UNICODE_FUNCTIONS[UCNV_NUMBER_OF_SUPPORTED_CONVERTER_TYPES] =

{
  T_UConverter_toUnicode_SBCS,
  T_UConverter_toUnicode_DBCS,
  T_UConverter_toUnicode_MBCS,
  T_UConverter_toUnicode_LATIN_1,
  T_UConverter_toUnicode_UTF8,
  T_UConverter_toUnicode_UTF16_BE,
  T_UConverter_toUnicode_UTF16_LE,
  T_UConverter_toUnicode_EBCDIC_STATEFUL,
  T_UConverter_toUnicode_ISO_2022
};

static T_ToUnicodeFunction TO_UNICODE_FUNCTIONS_OFFSETS_LOGIC[UCNV_NUMBER_OF_SUPPORTED_CONVERTER_TYPES] =

{
  NULL, /*UCNV_SBCS*/
  NULL, /*UCNV_DBCS*/
  T_UConverter_toUnicode_MBCS_OFFSETS_LOGIC,
  NULL, /*UCNV_LATIN_1*/
  T_UConverter_toUnicode_UTF8_OFFSETS_LOGIC,
  NULL, /*UTF16_BE*/
  NULL, /*UTF16_LE*/
  T_UConverter_toUnicode_EBCDIC_STATEFUL_OFFSETS_LOGIC,
  T_UConverter_toUnicode_ISO_2022_OFFSETS_LOGIC
};

static T_FromUnicodeFunction FROM_UNICODE_FUNCTIONS_OFFSETS_LOGIC[UCNV_NUMBER_OF_SUPPORTED_CONVERTER_TYPES] =

{
  NULL, /*UCNV_SBCS*/
  NULL, /*UCNV_DBCS*/
  T_UConverter_fromUnicode_MBCS_OFFSETS_LOGIC,
  NULL, /*UCNV_LATIN_1*/
  T_UConverter_fromUnicode_UTF8_OFFSETS_LOGIC,
  NULL, /*UTF16_BE*/
  NULL, /*UTF16_LE*/
  T_UConverter_fromUnicode_EBCDIC_STATEFUL_OFFSETS_LOGIC,
  T_UConverter_fromUnicode_ISO_2022_OFFSETS_LOGIC
};

static T_FromUnicodeFunction FROM_UNICODE_FUNCTIONS[UCNV_NUMBER_OF_SUPPORTED_CONVERTER_TYPES] =
{
  T_UConverter_fromUnicode_SBCS,
  T_UConverter_fromUnicode_DBCS,
  T_UConverter_fromUnicode_MBCS,
  T_UConverter_fromUnicode_LATIN_1,
  T_UConverter_fromUnicode_UTF8,
  T_UConverter_fromUnicode_UTF16_BE,
  T_UConverter_fromUnicode_UTF16_LE,
  T_UConverter_fromUnicode_EBCDIC_STATEFUL,
  T_UConverter_fromUnicode_ISO_2022
};

static T_GetNextUCharFunction GET_NEXT_UChar_FUNCTIONS[UCNV_NUMBER_OF_SUPPORTED_CONVERTER_TYPES] =
{
  T_UConverter_getNextUChar_SBCS,
  T_UConverter_getNextUChar_DBCS,
  T_UConverter_getNextUChar_MBCS,
  T_UConverter_getNextUChar_LATIN_1,
  T_UConverter_getNextUChar_UTF8,
  T_UConverter_getNextUChar_UTF16_BE,
  T_UConverter_getNextUChar_UTF16_LE,
  T_UConverter_getNextUChar_EBCDIC_STATEFUL,
  T_UConverter_getNextUChar_ISO_2022
};


void flushInternalUnicodeBuffer (UConverter * _this,
				 UChar * myTarget,
				 int32_t * myTargetIndex,
				 int32_t targetLength,
				 int32_t** offsets,
				 UErrorCode * err);

void flushInternalCharBuffer (UConverter * _this,
			      char *myTarget,
			      int32_t * myTargetIndex,
			      int32_t targetLength,
			      int32_t** offsets,
			      UErrorCode * err);


static void T_UConverter_fromCodepageToCodepage (UConverter * outConverter,
						 UConverter * inConverter,
						 char **target,
						 const char *targetLimit,
						 const char **source,
						 const char *sourceLimit,
						 int32_t* offsets,
						 bool_t flush,
						 UErrorCode * err);


const char* ucnv_getDefaultName ()
{
  return icu_getDefaultCodepage();
}

void   ucnv_setDefaultName (const char *converterName)
{
  icu_strcpy ((char*)icu_getDefaultCodepage(), converterName);
}
/*Calls through createConverter */
UConverter* ucnv_open (const char *name,
		       UErrorCode * err)
{
  if (U_FAILURE (*err))
    return NULL;

  /*In case "name" is NULL we want to open the default converter */
  if (name != NULL)
    return createConverter (name, err);
  else
    return createConverter (icu_getDefaultCodepage(), err);
}

/*Extracts the UChar* to a char* and calls through createConverter */
UConverter*  ucnv_openU (const UChar * name,
			 UErrorCode * err)
{
  char asciiName[UCNV_MAX_CONVERTER_NAME_LENGTH];
  
  if (U_FAILURE (*err))
    return NULL;
  if (name == NULL)
    return ucnv_open (NULL, err);
  if (u_strlen (name) > UCNV_MAX_CONVERTER_NAME_LENGTH)
    {
      *err = U_ILLEGAL_ARGUMENT_ERROR;
      return NULL;
    }
  return ucnv_open (u_austrcpy (asciiName, name), err);
}

/*Assumes a $platform-#codepage.$CONVERTER_FILE_EXTENSION scheme and calls
 *through createConverter*/
UConverter*  ucnv_openCCSID (int32_t codepage,
			     UConverterPlatform platform,
			     UErrorCode * err)
{
  char myName[UCNV_MAX_CONVERTER_NAME_LENGTH];

  if (U_FAILURE (*err))
    return NULL;

  copyPlatformString (myName, platform);
  icu_strcat (myName, "-");
  T_CString_integerToString (myName + icu_strlen (myName), codepage, 10);


  return createConverter (myName, err);
}

/*Decreases the reference counter in the shared immutable section of the object
 *and frees the mutable part*/

void ucnv_close (UConverter * converter)
{
  if (converter == NULL)
    return;
  if ((converter->sharedData->conversionType == UCNV_ISO_2022) &&
      (converter->mode == UCNV_SO))
    {
      ucnv_close (((UConverterDataISO2022 *) (converter->extraInfo))->currentConverter);
      icu_free (converter->extraInfo);
    }

  umtx_lock (NULL);
  converter->sharedData->referenceCounter--;
  umtx_unlock (NULL);
  icu_free (converter);

  return;
}

/*Frees all shared immutable objects that aren't referred to (reference count = 0)
 */
int32_t  ucnv_flushCache ()
{
  UConverterSharedData *mySharedData = NULL;
  int32_t pos = -1;
  int32_t tableDeletedNum = 0;

  /*if shared data hasn't even been lazy evaluated yet
   * return 0
   */
  if (SHARED_DATA_HASHTABLE == NULL)
    return 0;

  /*creates an enumeration to iterate through every element in the
   *table
   */
  umtx_lock (NULL);
  while (mySharedData = (UConverterSharedData *) uhash_nextElement (SHARED_DATA_HASHTABLE, &pos))
    {
      /*deletes only if reference counter == 0 */
      if (mySharedData->referenceCounter == 0)
	{
	  UErrorCode err = U_ZERO_ERROR;
	  tableDeletedNum++;

	  uhash_remove (SHARED_DATA_HASHTABLE, uhash_hashIString (mySharedData->name), &err);
	  deleteSharedConverterData (mySharedData);

	}
    }
  umtx_unlock (NULL);

  return tableDeletedNum;
}

/*returns a single Name from the static list, will return NULL if out of bounds
 */
const char*  ucnv_getAvailableName (int32_t index)
{
  UErrorCode err = U_ZERO_ERROR;
  /*lazy evaluates the list of Available converters */
  if (AVAILABLE_CONVERTERS_NAMES == NULL)
    setupAliasTableAndAvailableConverters (&err);
  if (index > AVAILABLE_CONVERTERS)
    return NULL;
  else
    return AVAILABLE_CONVERTERS_NAMES[index];
}

int32_t  ucnv_countAvailable ()
{
  UErrorCode err = U_ZERO_ERROR;
  /*lazy evaluates the list of Available converters */
  if (AVAILABLE_CONVERTERS_NAMES == NULL)
    setupAliasTableAndAvailableConverters (&err);

  return AVAILABLE_CONVERTERS;
}

void   ucnv_getSubstChars (const UConverter * converter,
			   char *mySubChar,
			   int8_t * len,
			   UErrorCode * err)
{
  if (U_FAILURE (*err))
    return;

  if (*len < converter->subCharLen)	/*not enough space in subChars */
    {
      *err = U_INDEX_OUTOFBOUNDS_ERROR;
      return;
    }

  icu_memcpy (mySubChar, converter->subChar, converter->subCharLen);	/*fills in the subchars */
  *len = converter->subCharLen;	/*store # of bytes copied to buffer */

  return;
}

void   ucnv_setSubstChars (UConverter * converter,
			   const char *mySubChar,
			   int8_t len,
			   UErrorCode * err)
{
  uint8_t x = 0;

  if (U_FAILURE (*err))
    return;

  /*Makes sure that the subChar is within the codepages char length boundaries */
  if ((len > converter->sharedData->maxBytesPerChar)
      || (len < converter->sharedData->minBytesPerChar))
    {
      *err = U_ILLEGAL_ARGUMENT_ERROR;
      return;
    }

  icu_memcpy (converter->subChar, mySubChar, len);	/*copies the subchars */
  converter->subCharLen = len;	/*sets the new len */

  return;
}




int32_t  ucnv_getDisplayName (const UConverter * converter,
			      const char *displayLocale,
			      UChar * displayName,
			      int32_t displayNameCapacity,
			      UErrorCode * err)
{
  UChar stringToWriteBuffer[UCNV_MAX_CONVERTER_NAME_LENGTH];
  UChar const *stringToWrite;
  int32_t stringToWriteLength;
  UResourceBundle *rb = NULL;

  if (U_FAILURE (*err))
    return 0;

  /*create an RB, init the fill-in string, gets it from the RB */
  rb = ures_open (NULL, displayLocale, err);

  stringToWrite = ures_get (rb,
			    converter->sharedData->name,
			    err);

  if (rb)
    ures_close (rb);

  if (U_SUCCESS (*err))
    stringToWriteLength = u_strlen (stringToWrite);
  else
    {
      /*Error While creating or getting resource from the resource bundle
       *use the internal name instead
       *
       *sets stringToWriteLength (which accounts for a NULL terminator)
       *and stringToWrite
       */
      stringToWriteLength = icu_strlen (converter->sharedData->name) + 1;
      stringToWrite = u_uastrcpy (stringToWriteBuffer, converter->sharedData->name);

      /*Hides the fallback to the internal name from the user */
      if (*err == U_MISSING_RESOURCE_ERROR)
	*err = U_ZERO_ERROR;
    }

  /*At this point we have a displayName and its length
   *we want to see if it fits in the user provided params
   */

  if (stringToWriteLength <= displayNameCapacity)
    {
      /*it fits */
      u_strcpy (displayName, stringToWrite);
    }
  else
    {
      /*it doesn't fit */
      *err = U_BUFFER_OVERFLOW_ERROR;

      u_strncpy (displayName,
		 stringToWrite,
		 displayNameCapacity);
      /*Zero terminates the string */
      if (displayNameCapacity > 0)
	displayName[displayNameCapacity - 1] = 0x0000;
    }

  /*if the user provided us with a with an outputLength
   *buffer we'll store in it the theoretical size of the
   *displayString
   */
  return stringToWriteLength;
}


/*resets the internal states of a converter
 *goal : have the same behaviour than a freshly created converter
 */
void  ucnv_reset (UConverter * converter)
{
  converter->toUnicodeStatus = converter->sharedData->defaultConverterValues.toUnicodeStatus;
  converter->fromUnicodeStatus = 0;
  converter->UCharErrorBufferLength = 0;
  converter->charErrorBufferLength = 0;
  if ((converter->sharedData->conversionType == UCNV_ISO_2022) &&
      (converter->mode == UCNV_SO))
    {
      converter->charErrorBufferLength = 3;
      converter->charErrorBuffer[0] = 0x1b;
      converter->charErrorBuffer[1] = 0x25;
      converter->charErrorBuffer[2] = 0x42;
      ucnv_close (((UConverterDataISO2022 *) (converter->extraInfo))->currentConverter);
      ((UConverterDataISO2022 *) (converter->extraInfo))->currentConverter = NULL;
      ((UConverterDataISO2022 *) (converter->extraInfo))->escSeq2022Length = 0;
    }
  converter->mode = UCNV_SI;

  return;
}

int8_t  ucnv_getMaxCharSize (const UConverter * converter)
{
  return converter->sharedData->maxBytesPerChar;
}


int8_t  ucnv_getMinCharSize (const UConverter * converter)
{
  return converter->sharedData->minBytesPerChar;
}

const char*  ucnv_getName (const UConverter * converter, UErrorCode * err)
     
{
  if (U_FAILURE (*err))
    return NULL;

  return converter->sharedData->name;
}

int32_t  ucnv_getCCSID (const UConverter * converter,
			UErrorCode * err)
{
  if (U_FAILURE (*err))
    return -1;

  return converter->sharedData->codepage;
}


UConverterPlatform  ucnv_getPlatform (const UConverter * converter,
				 UErrorCode * err)
{
  if (U_FAILURE (*err))
    return UCNV_UNKNOWN;
  
  return converter->sharedData->platform;
}

UConverterToUCallback  ucnv_getToUCallBack (const UConverter * converter)
{
  return converter->fromCharErrorBehaviour;
}

UConverterFromUCallback  ucnv_getFromUCallBack (const UConverter * converter)
{
  return converter->fromUCharErrorBehaviour;
}

UConverterToUCallback   ucnv_setToUCallBack (UConverter * converter,
					UConverterToUCallback action,
					UErrorCode * err)
{
  UConverterToUCallback myReturn = NULL;

  if (U_FAILURE (*err))
    return NULL;
  myReturn = converter->fromCharErrorBehaviour;
  converter->fromCharErrorBehaviour = action;

  return myReturn;
}

UConverterFromUCallback   ucnv_setFromUCallBack (UConverter * converter,
					    UConverterFromUCallback action,
					    UErrorCode * err)
{
  UConverterFromUCallback myReturn = NULL;
  
  if (U_FAILURE (*err))
    return NULL;
  myReturn = converter->fromUCharErrorBehaviour;
  converter->fromUCharErrorBehaviour = action;

  return myReturn;
}
#include <stdio.h>
void   ucnv_fromUnicode (UConverter * _this,
			 char **target,
			 const char *targetLimit,
			 const UChar ** source,
			 const UChar * sourceLimit,
			 int32_t* offsets,
			 bool_t flush,
			 UErrorCode * err)
{
  UConverterType myConvType;
  /*
   * Check parameters in for all conversions
   */
  if (U_FAILURE (*err))   return;
  if ((_this == NULL) || ((char *) targetLimit < *target) || (sourceLimit < *source))
    {
      *err = U_ILLEGAL_ARGUMENT_ERROR;
      return;
    }
  

  /*
   * Deal with stored carry over data.  This is done in the common location
   * to avoid doing it for each conversion.
   */
  if (_this->charErrorBufferLength > 0)
    {
      int32_t myTargetIndex = 0;

      flushInternalCharBuffer (_this, 
			       (char *) *target,
			       &myTargetIndex,
			       targetLimit - *target,
			       offsets?&offsets:NULL,
			       err);
      *target += myTargetIndex;
      if (U_FAILURE (*err)) return;
    }
  
  myConvType = _this->sharedData->conversionType;  
  if (offsets) 
    {
       int32_t targetSize = targetLimit - *target;
       int32_t i;
       switch (myConvType)
	 {
	 case UCNV_LATIN_1: case UCNV_SBCS : 
	   {
	     for (i=0; i<targetSize; i++) offsets[i] = i;
	     break;
	   }
	 case UCNV_UTF16_LittleEndian: case UCNV_UTF16_BigEndian: case UCNV_DBCS: 
	   {
	     --targetSize;
	     for (i=0; i<targetSize; i+=2) 
	       {
		 offsets[i] = i;
		 offsets[i+1] = i;
	       }
	     break;
	   }
	 default:
	   {
	     
	     FROM_UNICODE_FUNCTIONS_OFFSETS_LOGIC[(int) myConvType] (_this,
								     target,
								     targetLimit,
								     source,
								     sourceLimit,
								     offsets,
								     flush,
								     err);
	     return;
	   }
	 };    
    }
  /*calls the specific conversion routines */
  FROM_UNICODE_FUNCTIONS[(int)myConvType] (_this,
					   target,
					   targetLimit,
					   source,
					   sourceLimit,
					   offsets,
					   flush,
					   err);
  
  return;
}



void   ucnv_toUnicode (UConverter * _this,
		       UChar ** target,
		       const UChar * targetLimit,
		       const char **source,
		       const char *sourceLimit,
		       int32_t* offsets,
		       bool_t flush,
		       UErrorCode * err)
{
  /*
   * Check parameters in for all conversions
   */
  UConverterType myConvType;
  if (U_FAILURE (*err))   return;
  if ((_this == NULL) || ((UChar *) targetLimit < *target) || (sourceLimit < *source))
    {
      *err = U_ILLEGAL_ARGUMENT_ERROR;
      return;
    }

  myConvType = _this->sharedData->conversionType;
  /*
   * Deal with stored carry over data.  This is done in the common location
   * to avoid doing it for each conversion.
   */
  if (_this->UCharErrorBufferLength > 0)
    {
      int32_t myTargetIndex = 0;

      flushInternalUnicodeBuffer (_this, 
				  *target,
				  &myTargetIndex,
				  targetLimit - *target,
				  offsets?&offsets:NULL,
				  err);
      *target += myTargetIndex;
      if (U_FAILURE (*err))
	return;
    }

  if (offsets) 
    {
      int32_t targetSize = targetLimit - *target;
      int32_t i;

      switch (myConvType)
	{
	case UCNV_LATIN_1: case UCNV_SBCS : 
	  {
	    for (i=0; i<targetSize; i++) offsets[i] = i;
	    break;
	  }
	case UCNV_UTF16_LittleEndian: case UCNV_UTF16_BigEndian: case UCNV_DBCS: 
	  {
	    for (i=0; i<targetSize; i++) 
	      {
		offsets[i] = i*2;
	      }
	    break;
	  }
	default:
	  {
	    
	    TO_UNICODE_FUNCTIONS_OFFSETS_LOGIC[(int) myConvType] (_this,
								  target,
								  targetLimit,
								  source,
								  sourceLimit,
								  offsets,
								  flush,
								  err);
	    return;
	  }
	};
    }
  /*calls the specific conversion routines */
  TO_UNICODE_FUNCTIONS[(int) myConvType] (_this,
					  target,
					  targetLimit,
					  source,
					  sourceLimit,
					  offsets,
					  flush,
					  err);
  
  
  return;
}

int32_t   ucnv_fromUChars (const UConverter * converter,
			   char *target,
			   int32_t targetSize,
			   const UChar * source,
			   UErrorCode * err)
{
  const UChar *mySource = source;
  const UChar *mySource_limit;
  int32_t mySourceLength = 0;
  UConverter myConverter;
  char *myTarget = target;
  char *myTarget_limit;
  int32_t targetCapacity = 0;

  if (U_FAILURE (*err))
    return 0;

  if ((converter == NULL) || (targetSize < 0))
    {
      *err = U_ILLEGAL_ARGUMENT_ERROR;
      return 0;
    }

  /*makes a local copy of the UConverter */
  myConverter = *converter;


  /*Removes all state info on the UConverter */
  ucnv_reset (&myConverter);

  /*if the source is empty we return immediately */
  mySourceLength = u_strlen (source);
  if (mySourceLength == 0)
    {
      /*for consistency we still need to
       *store 0 in the targetCapacity
       *if the user requires it
       */
      return 0;
    }

  mySource_limit = mySource + mySourceLength;
  myTarget_limit = target + targetSize;

  if(myTarget_limit < target)       /*if targetsize is such that the limit*/
    myTarget_limit = (char *)U_MAX_PTR;     /* would wrap around, truncate it.    */

  if (targetSize > 0)
    {
      ucnv_fromUnicode (&myConverter,
			&myTarget,
			myTarget_limit,
			&mySource,
			mySource_limit,
			NULL,
			TRUE,
			err);
      targetCapacity = myTarget - target;
    }

  /*Updates targetCapacity to contain the number of bytes written to target */

  if (targetSize == 0)
    {
      *err = U_INDEX_OUTOFBOUNDS_ERROR;
    }

  /* If the output buffer is exhausted, we need to stop writing
   * to it but continue the conversion in order to store in targetSize
   * the number of bytes that was required*/
  if (*err == U_INDEX_OUTOFBOUNDS_ERROR)
    {
      char target2[CHUNK_SIZE];
      char *target2_alias = target2;
      const char *target2_limit = target2 + CHUNK_SIZE;

      /*We use a stack allocated buffer around which we loop
       *(in case the output is greater than CHUNK_SIZE)
       */

      while (*err == U_INDEX_OUTOFBOUNDS_ERROR)
	{
	  *err = U_ZERO_ERROR;
	  target2_alias = target2;
	  ucnv_fromUnicode (&myConverter,
			    &target2_alias,
			    target2_limit,
			    &mySource,
			    mySource_limit,
			    NULL,
			    TRUE,
			    err);

	  /*updates the output parameter to contain the number of char required */
	  targetCapacity += (target2_alias - target2) + 1;
	}
      /*We will set the erro code to U_BUFFER_OVERFLOW_ERROR only if
       *nothing graver happened in the previous loop*/
      (targetCapacity)--;
      if (U_SUCCESS (*err))
	*err = U_BUFFER_OVERFLOW_ERROR;
    }

  return targetCapacity;
}

int32_t ucnv_toUChars (const UConverter * converter,
		       UChar * target,
		       int32_t targetSize,
		       const char *source,
		       int32_t sourceSize,
		       UErrorCode * err)
{
  const char *mySource = source;
  const char *mySource_limit = source + sourceSize;
  UConverter myConverter;
  UChar *myTarget = target;
  UChar *myTarget_limit;
  int32_t targetCapacity;

  if (U_FAILURE (*err))
    return 0;

  if ((converter == NULL) || (targetSize < 0) || (sourceSize < 0))
    {
      *err = U_ILLEGAL_ARGUMENT_ERROR;
      return 0;
    }
  /*Means there is no work to be done */
  if (sourceSize == 0)
    {
      /*for consistency we still need to
       *store 0 in the targetCapacity
       *if the user requires it
       */
      if (targetSize >= 1)
	{
	  target[0] = 0x0000;
	  return 1;
	}
      else
	return 0;
    }

  /*makes a local copy of the UConverter */
  myConverter = *converter;

  /*Removes all state info on the UConverter */
  ucnv_reset (&myConverter);

  myTarget_limit = target + targetSize - 1;

  if(myTarget_limit < target)       /*if targetsize is such that the limit*/
    myTarget_limit = ((UChar*)U_MAX_PTR) - 1; /* would wrap around, truncate it.    */


  /*Not in pure pre-flight mode */
  if (targetSize > 0)
    {

      ucnv_toUnicode (&myConverter,
		      &myTarget,
		      myTarget_limit,	  /*Save a spot for the Null terminator */
		      &mySource,
		      mySource_limit,
		      NULL,
		      TRUE,
		      err);

      /*Null terminates the string */
      *(myTarget) = 0x0000;
    }


  /*Rigs targetCapacity to have at least one cell for zero termination */
  /*Updates targetCapacity to contain the number of bytes written to target */
  targetCapacity = 1;
  targetCapacity += myTarget - target;
  if (targetSize == 0)
    {
      *err = U_INDEX_OUTOFBOUNDS_ERROR;
    }
  /* If the output buffer is exhausted, we need to stop writing
   * to it but if the input buffer is not exhausted,
   * we need to continue the conversion in order to store in targetSize
   * the number of bytes that was required
   */
  if (*err == U_INDEX_OUTOFBOUNDS_ERROR)
    {
      UChar target2[CHUNK_SIZE];
      UChar *target2_alias = target2;
      const UChar *target2_limit = target2 + CHUNK_SIZE;

      /*We use a stack allocated buffer around which we loop
         (in case the output is greater than CHUNK_SIZE) */

      while (*err == U_INDEX_OUTOFBOUNDS_ERROR)
	{
	  *err = U_ZERO_ERROR;
	  target2_alias = target2;
	  ucnv_toUnicode (&myConverter,
			  &target2_alias,
			  target2_limit,
			  &mySource,
			  mySource_limit,
			  NULL,
			  TRUE,
			  err);

	  /*updates the output parameter to contain the number of char required */
	  targetCapacity += target2_alias - target2 + 1;
	}
      (targetCapacity)--;	/*adjust for last one */
      if (U_SUCCESS (*err))
	*err = U_BUFFER_OVERFLOW_ERROR;
    }

  return targetCapacity;
}

UChar ucnv_getNextUChar (UConverter * converter,
			 const char **source,
			 const char *sourceLimit,
			 UErrorCode * err)
{
  /* In case internal data had been stored
   * we return the first UChar in the internal buffer,
   * and update the internal state accordingly
   */
  if (converter->UCharErrorBufferLength > 0)
    {
      UChar myUChar = converter->UCharErrorBuffer[0];
      /*In this memmove we update the internal buffer by
       *popping the first character.
         *Note that in the call itself we decrement
         *UCharErrorBufferLength
       */
      icu_memmove (converter->UCharErrorBuffer,
		   converter->UCharErrorBuffer + 1,
		   --(converter->UCharErrorBufferLength) * sizeof (UChar));
      return myUChar;
    }
  /*calls the specific conversion routines */
  /*as dictated in a code review, avoids a switch statement */
  return GET_NEXT_UChar_FUNCTIONS[(int) (converter->sharedData->conversionType)] (converter,
										  source,
										  sourceLimit,
										  err);
}



/**************************
* Will convert a sequence of bytes from one codepage to another.
* @param toConverterName: The name of the converter that will be used to encode the output buffer
* @param fromConverterName: The name of the converter that will be used to decode the input buffer
* @param target: Pointer to the output buffer* written
* @param targetLength: on input contains the capacity of target, on output the number of bytes copied to target
* @param source: Pointer to the input buffer
* @param sourceLength: on input contains the capacity of source, on output the number of bytes processed in "source"
* @param internal: used internally to store store state data across calls
* @param err: fills in an error status
*/
void 
T_UConverter_fromCodepageToCodepage (UConverter * outConverter,
				     UConverter * inConverter,
				     char **target,
				     const char *targetLimit,
				     const char **source,
				     const char *sourceLimit,
				     int32_t* offsets,
				     bool_t flush,
				     UErrorCode * err)
{

  UChar out_chunk[CHUNK_SIZE];
  const UChar *out_chunk_limit = out_chunk + CHUNK_SIZE;
  UChar *out_chunk_alias;
  UChar const *out_chunk_alias2;


  if (U_FAILURE (*err))    return;


  /*loops until the input buffer is completely consumed
   *or if an error has be encountered
   *first we convert from inConverter codepage to Unicode
   *then from Unicode to outConverter codepage
   */
  while ((*source != sourceLimit) && U_SUCCESS (*err))
    {
      out_chunk_alias = out_chunk;
      ucnv_toUnicode (inConverter,
		      &out_chunk_alias,
		      out_chunk_limit,
		      source,
		      sourceLimit,
		      NULL,
		      flush,
		      err);

      /*U_INDEX_OUTOFBOUNDS_ERROR means that the output "CHUNK" is full
       *we will require at least another loop (it's a recoverable error)
       */

      if (U_SUCCESS (*err) || (*err == U_INDEX_OUTOFBOUNDS_ERROR))
	{
	  *err = U_ZERO_ERROR;
	  out_chunk_alias2 = out_chunk;

	  while ((out_chunk_alias2 != out_chunk_alias) && U_SUCCESS (*err))
	    {
	      ucnv_fromUnicode (outConverter,
				target,
				targetLimit,
				&out_chunk_alias2,
				out_chunk_alias,
				NULL,
				TRUE,
				err);

	    }
	}
      else
	break;
    }

  return;
}

int32_t  ucnv_convert(const char *toConverterName,
		      const char *fromConverterName,
		      char *target,
		      int32_t targetSize,
		      const char *source,
		      int32_t sourceSize,
		      UErrorCode * err)
{
  const char *mySource = source;
  const char *mySource_limit = source + sourceSize;
  int32_t mySourceLength = 0;
  UConverter *inConverter;
  UConverter *outConverter;
  char *myTarget = target;
  int32_t targetCapacity = 0;

  if (U_FAILURE (*err))
    return 0;

  if ((targetSize < 0) || (sourceSize < 0))
    {
      *err = U_ILLEGAL_ARGUMENT_ERROR;
      return 0;
    }

  /*if there is no input data, we're done */
  if (sourceSize == 0)
    {
      /*in case the caller passed an output ptr
       *we update it
       */
      return 0;
    }

  /*create the converters */
  inConverter = ucnv_open (fromConverterName, err);
  if (U_FAILURE (*err)) return 0;
  outConverter = ucnv_open (toConverterName, err);
  if (U_FAILURE (*err))
    {
      ucnv_close (inConverter);
      return 0;
    }


  if (targetSize > 0)
    {
      T_UConverter_fromCodepageToCodepage (outConverter,
					   inConverter,
					   &myTarget,
					   target + targetSize,
					   &mySource,
					   mySource_limit,
					   NULL,
					   TRUE,
					   err);
    }


  /*Updates targetCapacity to contain the number of bytes written to target */
  targetCapacity = myTarget - target;
  if (targetSize == 0)
    {
      *err = U_INDEX_OUTOFBOUNDS_ERROR;
    }

  /* If the output buffer is exhausted, we need to stop writing
   * to it but continue the conversion in order to store in targetSize
   * the number of bytes that was required*/
  if (*err == U_INDEX_OUTOFBOUNDS_ERROR)
    {
      char target2[CHUNK_SIZE];
      char *target2_alias = target2;
      const char *target2_limit = target2 + CHUNK_SIZE;

      /*We use a stack allocated buffer around which we loop
       *(in case the output is greater than CHUNK_SIZE)
       */

      while (*err == U_INDEX_OUTOFBOUNDS_ERROR)
	{
	  *err = U_ZERO_ERROR;
	  target2_alias = target2;
	  T_UConverter_fromCodepageToCodepage (outConverter,
					       inConverter,
					       &target2_alias,
					       target2_limit,
					       &mySource,
					       mySource_limit,
					       NULL,
					       TRUE,
					       err);

	  /*updates the output parameter to contain the number of char required */
	  targetCapacity += (target2_alias - target2) + 1;
	}
      /*We will set the erro code to U_BUFFER_OVERFLOW_ERROR only if
       *nothing graver happened in the previous loop*/
      (targetCapacity)--;
      if (U_SUCCESS (*err))
	*err = U_BUFFER_OVERFLOW_ERROR;
    }

  ucnv_close (inConverter);
  ucnv_close (outConverter);

  return targetCapacity;
}

UConverterType ucnv_getType(const UConverter* converter)
{
  return converter->sharedData->conversionType;
}

void ucnv_getStarters(const UConverter* converter, 
		      bool_t starters[256],
		      UErrorCode* err)
{
  if (U_FAILURE(*err)) return;
  /*Fire off an error if converter is not UCNV_MBCS*/
  if (converter->sharedData->conversionType != UCNV_MBCS) 
    {
      *err = U_ILLEGAL_ARGUMENT_ERROR;
      return;
    }
  
  /*fill's in the starters boolean array*/
  icu_memcpy(starters, converter->sharedData->table->mbcs.starters, 256*sizeof(bool_t));
  return;
}
