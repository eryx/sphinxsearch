//
// $Id$
//

//
// Copyright (c) 2001-2007, Andrew Aksyonoff. All rights reserved.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License. You should have
// received a copy of the GPL license along with this program; if you
// did not, you can find it at http://www.gnu.org/
//

#ifndef _sphinx_
#define _sphinx_

/////////////////////////////////////////////////////////////////////////////

#ifdef _WIN32
	#define USE_MYSQL		1	/// whether to compile MySQL support
	#define USE_WINDOWS		1	/// whether to compile for Windows
#else
	#define USE_WINDOWS		0	/// whether to compile for Windows
#endif

/////////////////////////////////////////////////////////////////////////////

#include "sphinxstd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#if USE_PGSQL
#include <libpq-fe.h>
#endif

#if USE_WINDOWS
#include <winsock2.h>
#else
#include <sys/types.h>
#include <unistd.h>
#endif

#if USE_MYSQL
#include <mysql.h>
#endif

#if USE_WINDOWS
typedef __int64				SphOffset_t;
#define STDOUT_FILENO		fileno(stdout)
#else
typedef off_t				SphOffset_t;
#endif

/////////////////////////////////////////////////////////////////////////////

#ifndef USE_64BIT
#define USE_64BIT 0
#endif

#if USE_64BIT

// use 64-bit unsigned integers to store document and word IDs
#define SPHINX_BITS_TAG	"-id64"
typedef uint64_t		SphWordID_t;
typedef uint64_t		SphDocID_t;

#define DOCID_MAX		U64C(0xffffffffffffffff) 
#define DOCID_FMT		"%" PRIu64
#define DOCINFO_IDSIZE	2

STATIC_SIZE_ASSERT ( SphWordID_t, 8 );
STATIC_SIZE_ASSERT ( SphDocID_t, 8 );

#else

// use 32-bit unsigned integers to store document and word IDs
#define SPHINX_BITS_TAG	""
typedef DWORD			SphWordID_t;
typedef DWORD			SphDocID_t;			

#define DOCID_MAX		0xffffffffUL
#define DOCID_FMT		"%u"
#define DOCINFO_IDSIZE	1

STATIC_SIZE_ASSERT ( SphWordID_t, 4 );
STATIC_SIZE_ASSERT ( SphDocID_t, 4 );

#endif // USE_64BIT

inline SphDocID_t &		DOCINFO2ID ( const DWORD * pDocinfo )	{ return *(SphDocID_t*)pDocinfo; }
inline DWORD *			DOCINFO2ATTRS ( DWORD * pDocinfo )		{ return pDocinfo+DOCINFO_IDSIZE; }

/////////////////////////////////////////////////////////////////////////////

#include "sphinxversion.h"

#define SPHINX_VERSION			"0.9.8" SPHINX_BITS_TAG "-dev (" SPH_SVN_TAGREV ")"
#define SPHINX_BANNER			"Sphinx " SPHINX_VERSION "\nCopyright (c) 2001-2007, Andrew Aksyonoff\n\n"
#define SPHINX_SEARCHD_PROTO	1

#define SPH_MAX_QUERY_WORDS		10
#define SPH_MAX_WORD_LEN		64
#define SPH_MAX_FILENAME_LEN	512
#define SPH_MAX_FIELDS			32

#define SPH_CACHE_WRITE			1048576

/////////////////////////////////////////////////////////////////////////////

/// time, in seconds
float			sphLongTimer ();

/// Sphinx CRC32 implementation
DWORD			sphCRC32 ( const BYTE * pString );

/// replaces all occurences of sMacro in sTemplate with textual representation of uValue
char *			sphStrMacro ( const char * sTemplate, const char * sMacro, SphDocID_t uValue );

/// try to obtain an exclusive lock on specified file
/// bWait specifies whether to wait
bool			sphLockEx ( int iFile, bool bWait );

/// remove existing locks
void			sphLockUn ( int iFile );

/// millisecond-precision sleep
void			sphUsleep ( int iMsec );

/// check if file exists and is a readable file
bool			sphIsReadable ( const char * sFilename, CSphString * pError=NULL );

/////////////////////////////////////////////////////////////////////////////
// TOKENIZERS
/////////////////////////////////////////////////////////////////////////////

extern const char *		SPHINX_DEFAULT_SBCS_TABLE;
extern const char *		SPHINX_DEFAULT_UTF8_TABLE;

/////////////////////////////////////////////////////////////////////////////

/// lowercaser remap range
struct CSphRemapRange
{
	int			m_iStart;
	int			m_iEnd;
	int			m_iRemapStart;

	CSphRemapRange ()
		: m_iStart		( -1 )
		, m_iEnd		( -1 )
		, m_iRemapStart	( -1 )
	{}

	CSphRemapRange ( int iStart, int iEnd, int iRemapStart )
		: m_iStart		( iStart )
		, m_iEnd		( iEnd )
		, m_iRemapStart	( iRemapStart )
	{}
};


inline bool operator < ( const CSphRemapRange & a, const CSphRemapRange & b )
{
	return a.m_iStart < b.m_iStart;
}


/// lowercaser
class CSphLowercaser
{
public:
				CSphLowercaser ();
				~CSphLowercaser ();

	void		Reset ();
	void		SetRemap ( const CSphLowercaser * pLC );
	void		AddRemaps ( const CSphRemapRange * pRemaps, int iRemaps, DWORD uFlags, DWORD uFlagsIfExists );
	void		AddSpecials ( const char * sSpecials );

public:
	const CSphLowercaser &		operator = ( const CSphLowercaser & rhs );

public:
	inline int	ToLower ( int iCode )
	{
		assert ( iCode>=0 );
		if ( iCode>=MAX_CODE )
			return 0;
		register int * pChunk = m_pChunk [ iCode>>CHUNK_BITS ];
		if ( pChunk )
			return pChunk [ iCode & CHUNK_MASK ];
		return 0;
	}

protected:
	static const int	CHUNK_COUNT	= 0x300;
	static const int	CHUNK_BITS	= 8;

	static const int	CHUNK_SIZE	= 1 << CHUNK_BITS;
	static const int	CHUNK_MASK	= CHUNK_SIZE - 1;
	static const int	MAX_CODE	= CHUNK_COUNT * CHUNK_SIZE;

	int					m_iChunks;					///< how much chunks are actually allocated
	int *				m_pData;					///< chunks themselves
	int *				m_pChunk [ CHUNK_COUNT ];	///< pointers to non-empty chunks
};

/////////////////////////////////////////////////////////////////////////////

/// synonym list entry
struct CSphSynonym
{
	CSphString	m_sFrom;	///< specially packed list of map-from tokens
	CSphString	m_sTo;		///< map-to string
	int			m_iFromLen;	///< cached m_sFrom length 
	int			m_iToLen;	///< cached m_sTo length 

	inline bool operator < ( const CSphSynonym & rhs ) const
	{
		return strcmp ( m_sFrom.cstr(), rhs.m_sFrom.cstr() ) < 0;
	}
};


/// generic tokenizer
class ISphTokenizer
{
public:
	/// trivial ctor
									ISphTokenizer() : m_iMinWordLen ( 1 ), m_iLastTokenLen ( 0 ) {}

	/// trivial dtor
	virtual							~ISphTokenizer () {}

	/// set new translation table
	/// returns true on success, false on failure
	virtual bool					SetCaseFolding ( const char * sConfig, CSphString & sError );

	/// add additional range to translation table
	virtual void					AddCaseFolding ( CSphRemapRange & tRange );

	/// add special chars to translation table (SBCS only, for now)
	/// updates lowercaser so that these remap to -1
	virtual void					AddSpecials ( const char * sSpecials );

	/// get lowercaser
	virtual const CSphLowercaser *	GetLowercaser () const { return &m_tLC; }

	/// set min word length
	virtual void					SetMinWordLen ( int iLen ) { m_iMinWordLen = Max ( iLen, 1 ); }

	/// get last token length, in codepoints
	int								GetLastTokenLen () { return m_iLastTokenLen; }

public:
	/// set n-gram characters (for CJK n-gram indexing)
	virtual bool					SetNgramChars ( const char *, CSphString & ) { return true; }

	/// set n-gram length (for CJK n-gram indexing)
	virtual void					SetNgramLen ( int ) {}

public:
	/// load synonyms list
	virtual bool					LoadSynonyms ( const char * sFilename, CSphString & sError );

public:
	/// pass next buffer
	virtual void					SetBuffer ( BYTE * sBuffer, int iLength, bool bLast ) = 0;

	/// get next token
	virtual BYTE *					GetToken () = 0;

	/// spawn a clone of my own
	virtual ISphTokenizer *			Clone () const = 0;

	/// SBCS or UTF-8?
	virtual bool					IsUtf8 () const = 0;

	/// calc codepoint length
	virtual int						GetCodepointLength ( int iCode ) const = 0;

protected:
	static const int				MAX_SYNONYM_LEN		= 1024;	///< max synonyms map-from length, bytes

	CSphLowercaser					m_tLC;						///< my lowercaser
	int								m_iMinWordLen;				///< minimal word length, in codepoints
	int								m_iLastTokenLen;			///< last token length, in codepoints

	CSphVector<CSphSynonym>			m_dSynonyms;				///< active synonyms
};

/// create SBCS tokenizer
ISphTokenizer *			sphCreateSBCSTokenizer ();

/// create UTF-8 tokenizer
ISphTokenizer *			sphCreateUTF8Tokenizer ();

/// create UTF-8 tokenizer with n-grams support (for CJK n-gram indexing)
ISphTokenizer *			sphCreateUTF8NgramTokenizer ();

/////////////////////////////////////////////////////////////////////////////
// DICTIONARIES
/////////////////////////////////////////////////////////////////////////////

/// abstract word dictionary interface
struct CSphDict
{
	/// virtualizing dtor
	virtual				~CSphDict () {}

	/// get word ID by word, "text" version
	/// may apply stemming and modify word inplac
	/// returns 0 for stopwords
	virtual SphWordID_t	GetWordID ( BYTE * pWord ) = 0;

	/// get word ID by word, "binary" version
	/// only used with prefix/infix indexing
	/// must not apply stemming and modify anything
	/// returns 0 for stopwords
	virtual SphWordID_t	GetWordID ( const BYTE * pWord, int iLen ) = 0;

	/// load stopwords from given files
	virtual void		LoadStopwords ( const char * sFiles, ISphTokenizer * pTokenizer ) = 0;

	/// set morphology
	virtual bool		SetMorphology ( const CSphVariant * sMorph, bool bUseUTF8, CSphString & sError ) = 0;
};


/// dictionary factory
CSphDict *				sphCreateDictionaryCRC ();

/////////////////////////////////////////////////////////////////////////////
// DATASOURCES
/////////////////////////////////////////////////////////////////////////////

/// hit info
struct CSphWordHit
{
	SphDocID_t		m_iDocID;		///< document ID
	SphWordID_t		m_iWordID;		///< word ID in current dictionary
	DWORD			m_iWordPos;		///< word position in current document
};

/// row entry (storage only, does not necessarily map 1:1 to attributes)
typedef DWORD		CSphRowitem;

const CSphRowitem	ROWITEM_MAX		= UINT_MAX;
const int			ROWITEM_BITS	= 8*sizeof(CSphRowitem);

STATIC_ASSERT ( sizeof(CSphRowitem)==sizeof(float), ROWITEM_AND_FLOAT_SIZE_MISMATCH );

/// setter
inline void sphSetRowAttr ( CSphRowitem * pRow, int iBitOffset, int iBitCount, CSphRowitem uValue )
{
	int iItem = iBitOffset / ROWITEM_BITS;
	if ( iBitCount==ROWITEM_BITS )
		pRow[iItem] = uValue;

	int iShift = iBitOffset % ROWITEM_BITS;
	CSphRowitem uMask = ( (1UL<<iBitCount)-1 ) << iShift;
	pRow[iItem] &= ~uMask;
	pRow[iItem] |= ( uMask & (uValue<<iShift) );
}


/// document info
struct CSphDocInfo
{
	SphDocID_t		m_iDocID;		///< document ID
	int				m_iRowitems;	///< row items count
	CSphRowitem *	m_pRowitems;	///< row data

	/// ctor. clears everything
	CSphDocInfo ()
		: m_iDocID ( 0 )
		, m_iRowitems ( 0 )
		, m_pRowitems ( NULL )
	{
	}

	/// copy ctor. just in case
	CSphDocInfo ( const CSphDocInfo & rhs )
		: m_iRowitems ( 0 )
		, m_pRowitems ( NULL )
	{
		*this = rhs;
	}

	/// dtor. frees everything
	~CSphDocInfo ()
	{
		SafeDeleteArray ( m_pRowitems );
	}

	/// reset
	void Reset ( int iNewRowitems )
	{
		m_iDocID = 0;
		if ( iNewRowitems!=m_iRowitems )
		{
			m_iRowitems = iNewRowitems;
			SafeDeleteArray ( m_pRowitems );
			if ( m_iRowitems )
				m_pRowitems = new CSphRowitem [ m_iRowitems ];
		}
	}

	/// assignment
	const CSphDocInfo & operator = ( const CSphDocInfo & rhs )
	{
		m_iDocID = rhs.m_iDocID;

		if ( m_iRowitems!=rhs.m_iRowitems )
		{
			SafeDeleteArray ( m_pRowitems );
			m_iRowitems = rhs.m_iRowitems;
			if ( m_iRowitems )
				m_pRowitems = new CSphRowitem [ m_iRowitems ]; // OPTIMIZE! pool these allocs
		}

		if ( m_iRowitems )
		{
			assert ( m_iRowitems==rhs.m_iRowitems );
			memcpy ( m_pRowitems, rhs.m_pRowitems, sizeof(CSphRowitem)*m_iRowitems );
		}

		return *this;
	}

	/// get attr by item index
	CSphRowitem GetAttr ( int iItem ) const
	{
		assert ( iItem>=0 && iItem<m_iRowitems );
		return m_pRowitems[iItem];
	}

	/// get attr by bit offset/count
	CSphRowitem GetAttr ( int iBitOffset, int iBitCount ) const
	{
		assert ( iBitOffset>=0 && iBitOffset<m_iRowitems*ROWITEM_BITS );
		assert ( iBitCount>0 && iBitOffset+iBitCount<=m_iRowitems*ROWITEM_BITS );

		int iItem = iBitOffset / ROWITEM_BITS;
		if ( iBitCount==ROWITEM_BITS )
			return m_pRowitems[iItem];

		int iShift = iBitOffset % ROWITEM_BITS;
		return ( m_pRowitems[iItem]>>iShift ) & ( (1UL<<iBitCount)-1 );
	}

	/// get float attr
	float GetAttrFloat ( int iItem ) const
	{
		assert ( iItem>=0 && iItem<m_iRowitems );
		return *( reinterpret_cast<float*> ( m_pRowitems+iItem ) );
	};

	/// set attr by bit offset/count
	void SetAttr ( int iBitOffset, int iBitCount, CSphRowitem uValue ) const
	{
		assert ( iBitOffset>=0 && iBitOffset<m_iRowitems*ROWITEM_BITS );
		assert ( iBitCount>0 && iBitOffset+iBitCount<=m_iRowitems*ROWITEM_BITS );
		sphSetRowAttr ( m_pRowitems, iBitOffset, iBitCount, uValue );
	}

	/// set float attr
	void SetAttrFloat ( int iItem, float fValue ) const
	{
		assert ( iItem>=0 && iItem<m_iRowitems );
		*( reinterpret_cast<float*> ( m_pRowitems+iItem ) ) = fValue;
	};
};


/// source statistics
struct CSphSourceStats
{
	/// how much documents
	int				m_iTotalDocuments;

	/// how much bytes
	SphOffset_t		m_iTotalBytes;

	/// ctor
	CSphSourceStats ()
	{
		Reset ();
	}

	/// reset
	void Reset ()
	{
		m_iTotalDocuments = 0;
		m_iTotalBytes = 0;
	}
};


/// known attribute types
enum
{
	SPH_ATTR_NONE		= 0,			///< not an attribute at all
	SPH_ATTR_INTEGER	= 1,			///< this attr is just an integer
	SPH_ATTR_TIMESTAMP	= 2,			///< this attr is a timestamp
	SPH_ATTR_ORDINAL	= 3,			///< this attr is an ordinal string number (integer at search time, specially handled at indexing time)
	SPH_ATTR_BOOL		= 4,			///< this attr is a boolean bit field
	SPH_ATTR_FLOAT		= 5,

	SPH_ATTR_MULTI		= 0x40000000UL	///< this attr has multiple values (0 or more)
};

/// known multi-valued attr sources
enum ESphAttrSrc
{
	SPH_ATTRSRC_NONE		= 0,	///< not multi-valued
	SPH_ATTRSRC_FIELD		= 1,	///< get attr values from text field
	SPH_ATTRSRC_QUERY		= 2,	///< get attr values from SQL query
	SPH_ATTRSRC_RANGEDQUERY	= 3		///< get attr values from ranged SQL query
};


/// source column info
struct CSphColumnInfo
{
	CSphString		m_sName;		///< column name
	DWORD			m_eAttrType;	///< attribute type

	int				m_iIndex;		///< index into source result set
	int				m_iRowitem;		///< index into document info row (only if attr spans whole rowitem; -1 otherwise)
	int				m_iBitOffset;	///< bit offset into row
	int				m_iBitCount;	///< bit count

	ESphAttrSrc		m_eSrc;			///< attr source (for multi-valued attrs only)
	CSphString		m_sQuery;		///< query to retrieve values (for multi-valued attrs only)
	CSphString		m_sQueryRange;	///< query to retrieve range (for multi-valued attrs only)

	/// handy ctor
	CSphColumnInfo ( const char * sName=NULL, DWORD eType=SPH_ATTR_NONE )
		: m_sName ( sName )
		, m_eAttrType ( eType )
		, m_iIndex ( -1 )
		, m_iRowitem ( -1 )
		, m_iBitOffset ( -1 )
		, m_iBitCount ( -1 )
		, m_eSrc ( SPH_ATTRSRC_NONE )
	{
		m_sName.ToLower ();
	}
};


/// schema comparison results
enum ESphSchemaCompare
{
	SPH_SCHEMAS_EQUAL			= 0,	///< these schemas are fully identical
	SPH_SCHEMAS_COMPATIBLE		= 1,	///< these schemas are compatible, ie. attribute types match
	SPH_SCHEMAS_INCOMPATIBLE	= 2		///< these schemas are not compatible
};

/// source schema
class CSphQuery;
struct CSphSchema
{
	CSphString						m_sName;		///< my human-readable name
	CSphVector<CSphColumnInfo>		m_dFields;		///< my fulltext-searchable fields

public:

	/// ctor
							CSphSchema ( const char * sName="(nameless)" ) : m_sName ( sName ) {}

	/// get field index by name
	/// returns -1 if not found
	int						GetFieldIndex ( const char * sName ) const;

	/// get attribute index by name
	/// returns -1 if not found
	int						GetAttrIndex ( const char * sName ) const;

	/// checks if two schemas match
	/// if result is not SPH_SCHEMAS_EQUAL, human-readable error/warning message is put to sError
	ESphSchemaCompare		CompareTo ( const CSphSchema & rhs, CSphString & sError ) const;


	/// reset fields and attrs
	void					Reset ();

	/// reset attrs only
	void					ResetAttrs ();

	/// get row size
	int						GetRowSize () const				{ return m_dRowUsed.GetLength(); }

	/// get attrs count
	int						GetAttrsCount () const			{ return m_dAttrs.GetLength(); }

	/// get non-virtual row size
	int						GetRealRowSize () const;

	/// get non-virtual attributes count
	int						GetRealAttrsCount () const;

	/// get attr
	const CSphColumnInfo &	GetAttr ( int iIndex ) const	{ return m_dAttrs[iIndex]; }

	/// add attr
	void					AddAttr ( const CSphColumnInfo & tAttr );

	/// build result schema from current contents and query
	/// adds virtual columns such as @group etc
	void					BuildResultSchema ( const CSphQuery * pQuery );

protected:
	CSphVector<CSphColumnInfo>		m_dAttrs;		///< all my attributes
	CSphVector<int>					m_dRowUsed;		///< row map (amount of used bits in each rowitem)
};


/// generic data source
class CSphHTMLStripper;
class CSphSource
{
public:
	CSphVector<CSphWordHit>				m_dHits;	///< current document split into words
	CSphDocInfo							m_tDocInfo;	///< current document info
	CSphVector<CSphString>				m_dStrAttrs;///< current document string attrs

public:
	/// ctor
										CSphSource ( const char * sName );

	/// dtor
	virtual								~CSphSource ();

	/// set dictionary
	void								SetDict ( CSphDict * dict );

	/// set HTML stripping mode
	/// sExtractAttrs defines what attributes to store
	/// sExtractAttrs format is "img=alt; a=alt,title"
	/// sExtractAttrs can be empty, this means that all the HTML will be stripped
	/// sExtractAttrs can be NULL, this means that no HTML stripping will be performed
	/// returns NULL on success
	/// returns error position on sConfig parsing failure
	const char *						SetStripHTML ( const char * sExtractAttrs );

	/// set tokenizer
	void								SetTokenizer ( ISphTokenizer * pTokenizer );

	/// get stats
	virtual const CSphSourceStats &		GetStats ();

	/// updates schema fields and attributes
	/// updates pInfo if it's empty; checks for match if it's not
	/// must be called after IterateHitsStart(); will always fail otherwise
	virtual bool						UpdateSchema ( CSphSchema * pInfo, CSphString & sError );

	/// configure source to emit prefixes or infixes
	/// passing zero iMinLength means to emit the words themselves
	void								SetEmitInfixes ( bool bPrefixesOnly, int iMinLength );

public:
	/// connect to the source (eg. to the database)
	/// connection settings are specific for each source type and as such
	/// are implemented in specific descendants
	virtual bool						Connect ( CSphString & sError ) = 0;

	/// disconnect from the source
	virtual void						Disconnect () = 0;

	/// check if there are any attributes configured
	/// note that there might be NO actual attributes in the case if configured
	/// ones do not match those actually returned by the source
	virtual bool						HasAttrsConfigured () = 0;

	/// begin iterating document hits
	/// to be implemented by descendants
	virtual bool						IterateHitsStart ( CSphString & sError ) = 0;

	/// get next document hit
	/// to be implemented by descendants
	/// on next document, returns true and m_tDocInfo.m_uDocID is not 0
	/// on end of documents, returns true and m_tDocInfo.m_uDocID is 0
	/// on error, returns false and fills sError
	virtual bool						IterateHitsNext ( CSphString & sError ) = 0;

	/// begin iterating values of out-of-document multi-valued attribute iAttr
	/// will fail if iAttr is out of range, or is not multi-valued
	/// can also fail if configured settings are invalid (eg. SQL query can not be executed)
	virtual bool						IterateMultivaluedStart ( int iAttr, CSphString & sError ) = 0;

	/// get next multi-valued (id,attr-value) tuple to m_tDocInfo
	virtual bool						IterateMultivaluedNext () = 0;

	/// post-index callback
	/// gets called when the indexing is succesfully (!) over
	virtual void						PostIndex () {}

protected:
	ISphTokenizer *						m_pTokenizer;	///< my tokenizer
	CSphDict *							m_pDict;		///< my dict
	
	CSphSourceStats						m_tStats;		///< my stats
	CSphSchema							m_tSchema;		///< my schema

	bool								m_bStripHTML;	///< whether to strip HTML
	CSphHTMLStripper *					m_pStripper;	///< my HTML stripper

	int									m_iMinInfixLen;	///< min indexable infix length (0 means don't index infixes)
	bool								m_bPrefixesOnly;///< whether to index prefixes only or all the infixes
};


/// generic document source
/// provides multi-field support and generic tokenizer
struct CSphSource_Document : CSphSource
{
	/// ctor
							CSphSource_Document ( const char * sName ) : CSphSource ( sName ) {}

	/// my generic tokenizer
	virtual bool			IterateHitsNext ( CSphString & sError );

	/// field data getter
	/// to be implemented by descendants
	virtual BYTE **			NextDocument ( CSphString & sError ) = 0;
};


/// generic SQL source params
struct CSphSourceParams_SQL
{
	// query params
	CSphString						m_sQuery;
	CSphString						m_sQueryRange;
	int								m_iRangeStep;

	CSphVector<CSphString>			m_dQueryPre;
	CSphVector<CSphString>			m_dQueryPost;
	CSphVector<CSphString>			m_dQueryPostIndex;
	CSphVector<CSphColumnInfo>		m_dAttrs;

	int								m_iRangedThrottle;

	// connection params
	CSphString						m_sHost;
	CSphString						m_sUser;
	CSphString						m_sPass;
	CSphString						m_sDB;
	int								m_iPort;

	CSphSourceParams_SQL ();
};


/// generic SQL source
/// multi-field plain-text documents fetched from given query
struct CSphSource_SQL : CSphSource_Document
{
						CSphSource_SQL ( const char * sName );
	virtual				~CSphSource_SQL () {}

	bool				Setup ( const CSphSourceParams_SQL & pParams );
	virtual bool		Connect ( CSphString & sError );
	virtual void		Disconnect ();

	virtual bool		IterateHitsStart ( CSphString & sError );
	virtual BYTE **		NextDocument ( CSphString & sError );
	virtual void		PostIndex ();

	virtual bool		HasAttrsConfigured () { return m_tParams.m_dAttrs.GetLength()!=0; }

	virtual bool		IterateMultivaluedStart ( int iAttr, CSphString & sError );
	virtual bool		IterateMultivaluedNext ();

private:
	bool				m_bSqlConnected;///< am i connected?

protected:
	CSphString			m_sSqlDSN;

	BYTE *				m_dFields [ SPH_MAX_FIELDS ];

	SphDocID_t			m_uMinID;		///< grand min ID
	SphDocID_t			m_uMaxID;		///< grand max ID
	SphDocID_t			m_uCurrentID;	///< current min ID
	SphDocID_t			m_uMaxFetchedID;///< max actually fetched ID
	int					m_iMultiAttr;	///< multi-valued attr being currently fetched

	CSphSourceParams_SQL		m_tParams;

	static const int			MACRO_COUNT = 2;
	static const char * const	MACRO_VALUES [ MACRO_COUNT ];

protected:
	bool					RunQueryStep ( CSphString & sError );

protected:
	virtual void			SqlDismissResult () = 0;
	virtual bool			SqlQuery ( const char * sQuery ) = 0;
	virtual bool			SqlIsError () = 0;
	virtual const char *	SqlError () = 0;
	virtual bool			SqlConnect () = 0;
	virtual void			SqlDisconnect () = 0;
	virtual int				SqlNumFields() = 0;
	virtual bool			SqlFetchRow() = 0;
	virtual const char *	SqlColumn ( int iIndex ) = 0;
	virtual const char *	SqlFieldName ( int iIndex ) = 0;
};


#if USE_MYSQL
/// MySQL source params
struct CSphSourceParams_MySQL : CSphSourceParams_SQL
{
	CSphString	m_sUsock;					///< UNIX socket
				CSphSourceParams_MySQL ();	///< ctor. sets defaults
};


/// MySQL source implementation
/// multi-field plain-text documents fetched from given query
struct CSphSource_MySQL : CSphSource_SQL
{
							CSphSource_MySQL ( const char * sName );
	bool					Setup ( const CSphSourceParams_MySQL & tParams );

protected:
	MYSQL_RES *				m_pMysqlResult;
	MYSQL_FIELD *			m_pMysqlFields;
	MYSQL_ROW				m_tMysqlRow;
	MYSQL					m_tMysqlDriver;

	CSphString				m_sMysqlUsock;

protected:
	virtual void			SqlDismissResult ();
	virtual bool			SqlQuery ( const char * sQuery );
	virtual bool			SqlIsError ();
	virtual const char *	SqlError ();
	virtual bool			SqlConnect ();
	virtual void			SqlDisconnect ();
	virtual int				SqlNumFields();
	virtual bool			SqlFetchRow();
	virtual const char *	SqlColumn ( int iIndex );
	virtual const char *	SqlFieldName ( int iIndex );
};
#endif // USE_MYSQL


#if USE_PGSQL
/// PgSQL specific source params
struct CSphSourceParams_PgSQL : CSphSourceParams_SQL
{
	CSphString		m_sClientEncoding;
					CSphSourceParams_PgSQL ();
};


/// PgSQL source implementation
/// multi-field plain-text documents fetched from given query
struct CSphSource_PgSQL : CSphSource_SQL
{
							CSphSource_PgSQL ( const char * sName );
	bool					Setup ( const CSphSourceParams_PgSQL & pParams );

protected:
	PGresult * 				m_pPgResult;	///< postgresql execution restult context
	PGconn *				m_tPgDriver;	///< postgresql connection context

	int						m_iPgRows;		///< how much rows last step returned
	int						m_iPgRow;		///< current row (0 based, as in PQgetvalue)

	CSphString				m_sPgClientEncoding;

protected:
	virtual void			SqlDismissResult ();
	virtual bool			SqlQuery ( const char * sQuery );
	virtual bool			SqlIsError ();
	virtual const char *	SqlError ();
	virtual bool			SqlConnect ();
	virtual void			SqlDisconnect ();
	virtual int				SqlNumFields();
	virtual bool			SqlFetchRow();
	virtual const char *	SqlColumn ( int iIndex );
	virtual const char *	SqlFieldName ( int iIndex );
};
#endif // USE_PGSQL


/// XML pipe source implementation
class CSphSource_XMLPipe : public CSphSource
{
public:
					CSphSource_XMLPipe ( const char * sName );	///< ctor
					~CSphSource_XMLPipe ();						///< dtor

	bool			Setup ( const char * sCommand );			///< memorize the command
	virtual bool	Connect ( CSphString & sError );			///< run the command and open the pipe
	virtual void	Disconnect ();								///< close the pipe

	virtual bool	IterateHitsStart ( CSphString & ) { return true; }	///< Connect() starts getting documents automatically, so this one is empty
	virtual bool	IterateHitsNext ( CSphString & sError );			///< parse incoming chunk and emit some hits

	virtual bool	HasAttrsConfigured ()							{ return true; }	///< xmlpipe always has some attrs for now
	virtual bool	IterateMultivaluedStart ( int, CSphString & )	{ return false; }	///< xmlpipe does not support multi-valued attrs for now
	virtual bool	IterateMultivaluedNext ()						{ return false; }	///< xmlpipe does not support multi-valued attrs for now

private:
	enum Tag_e
	{
		TAG_DOCUMENT = 0,
		TAG_ID,
		TAG_GROUP,
		TAG_TITLE,
		TAG_BODY
	};

private:
	CSphString		m_sCommand;			///< my command

	bool			m_bBody;			///< are we scanning body or expecting document?
	Tag_e			m_eTag;				///< what's our current tag
	const char *	m_pTag;				///< tag name
	int				m_iTagLength;		///< tag name length

	FILE *			m_pPipe;			///< incoming stream
	BYTE			m_sBuffer [ 4096 ];	///< buffer
	BYTE *			m_pBuffer;			///< current buffer pos
	BYTE *			m_pBufferEnd;		///< buffered end pos
	
	int				m_iWordPos;			///< current word position

private:
	/// set current tag
	void			SetTag ( const char * sTag );

	/// read in some more data
	/// moves everything from current ptr (m_pBuffer) to the beginng
	/// reads in as much data as possible to the end
	/// returns false on EOF
	bool			UpdateBuffer ();

	/// skips whitespace
	/// does buffer updates
	/// returns false on EOF
	bool			SkipWhitespace ();

	/// check if what's at current pos is either opening/closing current tag (m_pTag)
	/// return false on failure
	bool			CheckTag ( bool bOpen, CSphString & sError );

	/// skips whitespace and opening/closing current tag (m_pTag)
	/// returns false on failure
	bool			SkipTag ( bool bOpen, CSphString & sError );

	/// scan for tag with integer value
	bool			ScanInt ( const char * sTag, DWORD * pRes, CSphString & sError );

	/// scan for tag with integer value
	bool			ScanInt ( const char * sTag, uint64_t * pRes, CSphString & sError );

	/// scan for tag with string value
	bool			ScanStr ( const char * sTag, char * pRes, int iMaxLength, CSphString & sError );
};

/////////////////////////////////////////////////////////////////////////////
// SEARCH QUERIES
/////////////////////////////////////////////////////////////////////////////

/// search query match
struct CSphMatch : public CSphDocInfo
{
	int		m_iWeight;	///< my computed weight
	float	m_fGeodist;	///< my computed geodistance
	int		m_iTag;		///< my index tag

	CSphMatch () : m_iWeight ( 0 ), m_fGeodist ( 0 ), m_iTag ( 0 ) {}
	bool operator == ( const CSphMatch & rhs ) const { return ( m_iDocID==rhs.m_iDocID ); }
};


/// specialized swapper
inline void Swap ( CSphMatch & a, CSphMatch & b )
{
	Swap ( a.m_iDocID, b.m_iDocID );
	Swap ( a.m_iRowitems, b.m_iRowitems );
	Swap ( a.m_pRowitems, b.m_pRowitems );
	Swap ( a.m_iWeight, b.m_iWeight );
	Swap ( a.m_iTag, b.m_iTag );
	Swap ( a.m_fGeodist, b.m_fGeodist );
}


/// search query sorting orders
enum ESphSortOrder
{
	SPH_SORT_RELEVANCE		= 0,	///< sort by document relevance desc, then by date
	SPH_SORT_ATTR_DESC		= 1,	///< sort by document date desc, then by relevance desc
	SPH_SORT_ATTR_ASC		= 2,	///< sort by document date asc, then by relevance desc
	SPH_SORT_TIME_SEGMENTS	= 3,	///< sort by time segments (hour/day/week/etc) desc, then by relevance desc
	SPH_SORT_EXTENDED		= 4,	///< sort by SQL-like expression (eg. "@relevance DESC, price ASC, @id DESC")

	SPH_SORT_TOTAL
};


/// search query matching mode
enum ESphMatchMode
{
	SPH_MATCH_ALL = 0,			///< match all query words
	SPH_MATCH_ANY,				///< match any query word
	SPH_MATCH_PHRASE,			///< match this exact phrase
	SPH_MATCH_BOOLEAN,			///< match this boolean query
	SPH_MATCH_EXTENDED,			///< match this extended query

	SPH_MATCH_TOTAL
};


/// search query grouping mode
enum ESphGroupBy
{
	SPH_GROUPBY_DAY		= 0,	///< group by day
	SPH_GROUPBY_WEEK	= 1,	///< group by week
	SPH_GROUPBY_MONTH	= 2,	///< group by month
	SPH_GROUPBY_YEAR	= 3,	///< group by year
	SPH_GROUPBY_ATTR	= 4,	///< group by attribute value
	SPH_GROUPBY_ATTRPAIR= 5		///< group by sequential attrs pair
};


/// search query filter types
enum ESphFilter
{
	SPH_FILTER_VALUES		= 0,	///< filter by integer values set
	SPH_FILTER_RANGE		= 1,	///< filter by integer range
	SPH_FILTER_FLOATRANGE	= 2		///< filter by float range
};


/// search query filter
class CSphFilter
{
public:
	CSphString			m_sAttrName;	///< filtered attribute name
	bool				m_bExclude;		///< whether this is "include" or "exclude" filter (default is "include")

	ESphFilter			m_eType;		///< filter type
	union
	{
		DWORD			m_uMinValue;	///< range min
		float			m_fMinValue;	///< range min
	};
	union
	{
		DWORD			m_uMaxValue;	///< range max
		float			m_fMaxValue;	///< range max
	};
	CSphVector<DWORD>	m_dValues;		///< integer values set

public:
	bool				m_bMva;			///< whether this filter is against multi-valued attribute
	int					m_iRowitem;		///< attr item offset into row, for full-item attrs
	int					m_iBitOffset;	///< attr bit offset into row
	int					m_iBitCount;	///< attr bit count

public:
						CSphFilter ();

	bool				operator == ( const CSphFilter & rhs ) const;
	bool				operator != ( const CSphFilter & rhs ) const { return !( (*this)==rhs ); }

protected:
						CSphFilter ( const CSphFilter & rhs );
};


/// search query
class CSphQuery
{
public:
	CSphString		m_sIndexes;		///< indexes to search
	CSphString		m_sQuery;		///< query string

	int				m_iOffset;		///< offset into result set (as X in MySQL LIMIT X,Y clause)
	int				m_iLimit;		///< limit into result set (as Y in MySQL LIMIT X,Y clause)
	DWORD *			m_pWeights;		///< user-supplied per-field weights. may be NULL. default is NULL. NOT OWNED, WILL NOT BE FREED in dtor.
	int				m_iWeights;		///< number of user-supplied weights. missing fields will be assigned weight 1. default is 0
	ESphMatchMode	m_eMode;		///< match mode. default is "match all"
	ESphSortOrder	m_eSort;		///< sort mode
	CSphString		m_sSortBy;		///< attribute to sort by
	int				m_iMaxMatches;	///< max matches to retrieve, default is 1000. more matches use more memory and CPU time to hold and sort them

	SphDocID_t		m_iMinID;		///< min ID to match, 0 by default
	SphDocID_t		m_iMaxID;		///< max ID to match, UINT_MAX by default

	CSphVector<CSphFilter>	m_dFilters;	///< filters

	CSphString		m_sGroupBy;		///< group-by attribute name
	ESphGroupBy		m_eGroupFunc;	///< function to pre-process group-by attribute value with
	CSphString		m_sGroupSortBy;	///< sorting clause for groups in group-by mode
	CSphString		m_sGroupDistinct;///< count distinct values for this attribute

	int				m_iCutoff;		///< matches count threshold to stop searching at (defualt is 0; means to search until all matches are found)

	int				m_iRetryCount;	///< retry count, for distributed queries
	int				m_iRetryDelay;	///< retry delay, for distributed queries

public:
	bool			m_bGeoAnchor;		///< do we have an anchor
	CSphString		m_sGeoLatAttr;		///< latitude attr name
	CSphString		m_sGeoLongAttr;		///< longitude attr name
	float			m_fGeoLatitude;		///< anchor latitude
	float			m_fGeoLongitude;	///< anchor longitude

public:
	bool			m_bCalcGeodist;		///< whether this query needs to calc @geodist

public:
	int				m_iPresortRowitems;	///< row size submitted to sorter (with calculated attributes, but without groupby/count attributes added by sorters)
	int				m_iGroupbyOffset;	///< group-by attr bit offset
	int				m_iGroupbyCount;	///< group-by attr bit count
	int				m_iDistinctOffset;	///< distinct-counted attr bit offset
	int				m_iDistinctCount;	///< distinct-counted attr bit count

public:
	int				m_iOldVersion;		///< version, to fixup old queries
	int				m_iOldGroups;		///< 0.9.6 group filter values count
	DWORD *			m_pOldGroups;		///< 0.9.6 group filter values
	DWORD			m_iOldMinTS;		///< 0.9.6 min timestamp
	DWORD			m_iOldMaxTS;		///< 0.9.6 max timestamp
	DWORD			m_iOldMinGID;		///< 0.9.6 min group id
	DWORD			m_iOldMaxGID;		///< 0.9.6 max group id

public:
					CSphQuery ();		///< ctor, fills defaults
					~CSphQuery () {}	///< dtor, frees owned stuff
};


/// search query result
class CSphQueryResult
{
public:
	struct WordStat_t
	{
		CSphString			m_sWord;	///< i-th search term (normalized word form)
		int					m_iDocs;	///< document count for this term
		int					m_iHits;	///< hit count for this term
	}						m_tWordStats [ SPH_MAX_QUERY_WORDS ];

	int						m_iNumWords;		///< query word count
	int						m_iQueryTime;		///< query time, ms
	CSphVector<CSphMatch>	m_dMatches;			///< top matching documents, no more than MAX_MATCHES
	int						m_iTotalMatches;	///< total matches count

	CSphSchema				m_tSchema;			///< result schema
	const DWORD *			m_pMva;				///< pointer to MVA storage

	CSphString				m_sError;			///< error message
	CSphString				m_sWarning;			///< warning message

	int						m_iOffset;			///< requested offset into matches array
	int						m_iCount;			///< count which will be actually served (computed from total, offset and limit)

	int						m_iSuccesses;

public:
							CSphQueryResult ();		///< ctor
	virtual					~CSphQueryResult ();	///< dtor, which releases all owned stuff
};

/////////////////////////////////////////////////////////////////////////////
// ATTRIBUTE UPDATE QUERY
/////////////////////////////////////////////////////////////////////////////

struct CSphAttrUpdate_t
{
	CSphVector<CSphColumnInfo>		m_dAttrs;		///< update schema (ie. what attrs to update)
	int								m_iUpdates;		///< updates count
	DWORD *							m_pUpdates;		///< updates data

public:
	CSphAttrUpdate_t ();		///< builds new clean structure
	~CSphAttrUpdate_t ();		
};

/////////////////////////////////////////////////////////////////////////////
// FULLTEXT INDICES
/////////////////////////////////////////////////////////////////////////////

/// progress info
struct CSphIndexProgress
{
	enum Phase_e
	{
		PHASE_COLLECT,				///< document collection phase
		PHASE_SORT,					///< final sorting phase
		PHASE_COLLECT_MVA,			///< multi-valued attributes collection phase
		PHASE_SORT_MVA,				///< multi-valued attributes collection phase
		PHASE_MERGE					///< index merging phase
	};

	Phase_e			m_ePhase;		///< current indexing phase

	int				m_iDocuments;	///< PHASE_COLLECT: documents collected so far
	SphOffset_t		m_iBytes;		///< PHASE_COLLECT: bytes collected so far

	uint64_t		m_iAttrs;		///< PHASE_COLLECT_MVA, PHASE_SORT_MVA: attrs processed so far
	uint64_t		m_iAttrsTotal;	///< PHASE_SORT_MVA: attrs total

	SphOffset_t		m_iHits;		///< PHASE_SORT: hits sorted so far
	SphOffset_t		m_iHitsTotal;	///< PHASE_SORT: hits total

	int				m_iWords;		///< PHASE_MERGE: words merged so far

	CSphIndexProgress ()
		: m_ePhase ( PHASE_COLLECT )
		, m_iDocuments ( 0 )
		, m_iBytes ( 0 )
		, m_iAttrs ( 0 )
		, m_iAttrsTotal ( 0 )
		, m_iHits ( 0 )
		, m_iHitsTotal ( 0 )
		, m_iWords ( 0 )
	{}
};


/// match comparator state
struct CSphMatchComparatorState
{
	static const int	MAX_ATTRS = 5;

	int					m_iAttr[MAX_ATTRS];			///< sort-by attr index
	int					m_iRowitem[MAX_ATTRS];		///< sort-by attr row item (-1 if not maps to full item)
	int					m_iBitOffset[MAX_ATTRS];	///< sort-by attr bit offset into row
	int					m_iBitCount[MAX_ATTRS];		///< sort-by attr bit count

	DWORD				m_uAttrDesc;				///< sort order mask (if i-th bit is set, i-th attr order is DESC)
	DWORD				m_iNow;						///< timestamp (for timesegments sorting mode)

	/// create default empty state
	CSphMatchComparatorState ()
		: m_uAttrDesc ( 0 )
		, m_iNow ( 0 )
	{
		for ( int i=0; i<MAX_ATTRS; i++ )
		{
			m_iAttr[i] = -1;
			m_iRowitem[i] = -1;
			m_iBitOffset[i] = -1;
			m_iBitCount[i] = -1;
		}
	}

	/// get my i-th attr from match
	template<bool BITS> inline CSphRowitem GetAttr ( const CSphMatch & m, int i ) const;

	/// check if any of my attrs are bitfields
	bool UsesBitfields ()
	{
		for ( int i=0; i<MAX_ATTRS; i++ )
			if ( m_iAttr[i]>=0 )
				if ( m_iBitCount[i]!=ROWITEM_BITS || (m_iBitOffset[i]%ROWITEM_BITS )!=0 )
					return true;
		return false;
	}
};

template<>
inline CSphRowitem CSphMatchComparatorState::GetAttr<false> ( const CSphMatch & m, int i ) const
{
	return m.GetAttr ( m_iRowitem[i] );
}

template<>
inline CSphRowitem CSphMatchComparatorState::GetAttr<true> ( const CSphMatch & m, int i ) const
{
	return m.GetAttr ( m_iBitOffset[i], m_iBitCount[i] );
}


/// generic match sorter interface
class ISphMatchSorter
{
public:
	bool				m_bRandomize;
	int					m_iTotal;

public:
	/// ctor
						ISphMatchSorter () : m_bRandomize ( false ), m_iTotal ( 0 ) {}

	/// virtualizing dtor
	virtual				~ISphMatchSorter () {}

	/// check if this sorter needs attr values
	virtual bool		UsesAttrs () = 0;

	/// set match comparator state
	virtual void		SetState ( const CSphMatchComparatorState & ) = 0;

	/// set group comparator state
	virtual void		SetGroupState ( const CSphMatchComparatorState & ) {}

	/// base push
	/// returns false if the entry was rejected as duplicate
	/// returns true otherwise (even if it was not actually inserted)
	virtual bool		Push ( const CSphMatch & tEntry ) = 0;

	/// get entries count
	virtual int			GetLength () const = 0;

	/// get total count of non-duplicates Push()ed through this queue
	virtual int			GetTotalCount () const { return m_iTotal; }

	/// get first entry ptr
	/// used for docinfo lookup
	/// entries order does NOT matter and is NOT guaranteed
	/// however top GetLength() entries MUST be stored linearly starting from First()
	virtual CSphMatch *	First () = 0;

	/// store all entries into specified location and remove them from the queue
	/// entries are stored in properly sorted order,
	/// if iTag is non-negative, entries are also tagged; otherwise, their tag's unchanged
	virtual void		Flatten ( CSphMatch * pTo, int iTag ) = 0;
};


/// available docinfo storage strategies
enum ESphDocinfo
{
	SPH_DOCINFO_NONE		= 0,	///< no docinfo available
	SPH_DOCINFO_INLINE		= 1,	///< inline docinfo into index (specifically, into doclists)
	SPH_DOCINFO_EXTERN		= 2		///< store docinfo separately
};

/// purging data
struct CSphPurgeData
{
	CSphString		m_sKey;
	int				m_iAttrIndex;
	DWORD			m_dwMinValue;
	DWORD			m_dwMaxValue;
	bool			m_bPurge;

	CSphPurgeData()
		: m_iAttrIndex ( -1 )
		, m_dwMinValue ( 0 )
		, m_dwMaxValue ( 0 )
		, m_bPurge ( false )
	{}

	bool IsShouldPurge ( const DWORD * pAttrs )
	{
		if ( ( m_iAttrIndex == -1 ) || !m_bPurge || !pAttrs )
			return false;
		else
			return ( ( m_dwMinValue > pAttrs[m_iAttrIndex] ) || ( m_dwMaxValue < pAttrs[m_iAttrIndex] ) );
	}

	bool IsEnabled ()
	{
		return m_bPurge;
	}
};

/// generic fulltext index interface
class CSphIndex
{
public:
	typedef void ProgressCallback_t ( const CSphIndexProgress * pStat, bool bPhaseEnd );

public:
								CSphIndex ( const char * sName );
	virtual						~CSphIndex () {}

	virtual const CSphString &	GetLastError () const { return m_sLastError; }
	virtual const CSphSchema *	GetSchema () const { return &m_tSchema; }

	virtual	void				SetProgressCallback ( ProgressCallback_t * pfnProgress ) { m_pProgress = pfnProgress; }
	virtual void				SetInfixIndexing ( bool bPrefixesOnly, int iMinLength );

public:
	/// build index by indexing given sources
	virtual int					Build ( CSphDict * dict, const CSphVector<CSphSource*> & dSources, int iMemoryLimit, ESphDocinfo eDocinfo ) = 0;

	/// build index by mering current index with given index
	virtual bool				Merge ( CSphIndex * pSource, CSphPurgeData & tPurgeData ) = 0;

public:
	/// check all data files, preload schema, and preallocate enough shared RAM to load memory-cached data
	virtual const CSphSchema *	Prealloc ( bool bMlock, CSphString * sWarning ) = 0;

	/// deallocate all previously preallocated shared data
	virtual void				Dealloc () = 0;

	/// precache everything which needs to be precached
	// WARNING, WILL BE CALLED FROM DIFFERENT PROCESS, MUST ONLY MODIFY SHARED MEMORY
	virtual bool				Preread () = 0;

	/// set new index base path
	virtual void				SetBase ( const char * sNewBase ) = 0;

	/// set new index base path, and physically rename index files too
	virtual bool				Rename ( const char * sNewBase ) = 0;

	/// obtain exclusive lock on this index
	virtual bool				Lock () = 0;

	/// dismiss exclusive lock and unlink lock file
	virtual void				Unlock () = 0;

	/// relock shared RAM (only on daemonization)
	virtual bool				Mlock () = 0;

public:
	virtual CSphQueryResult *	Query ( ISphTokenizer * pTokenizer, CSphDict * pDict, CSphQuery * pQuery ) = 0;
	virtual bool				QueryEx ( ISphTokenizer * pTokenizer, CSphDict * pDict, CSphQuery * pQuery, CSphQueryResult * pResult, ISphMatchSorter * pTop ) = 0;
	virtual bool				MultiQuery ( ISphTokenizer * pTokenizer, CSphDict * pDict, CSphQuery * pQuery, CSphQueryResult * pResult, int iSorters, ISphMatchSorter ** ppSorters ) = 0;

public:
	/// updates memory-cached attributes in real time
	/// returns non-negative amount of actually found and updated records on success
	/// on failure, -1 is returned and GetLastError() contains error message
	virtual int					UpdateAttributes ( const CSphAttrUpdate_t & tUpd ) = 0;

	/// saves memory-cached attributes, if there were any updates to them
	/// on failure, false is returned and GetLastError() contains error message
	virtual bool				SaveAttributes () = 0;

	/// externally set "updated" flag
	/// needed because updates and saves may be performed by other processes
	virtual void				SetAttrsUpdated ( bool bFlag ) { m_bAttrsUpdated = bFlag; }

protected:
	ProgressCallback_t *		m_pProgress;
	CSphSchema					m_tSchema;
	CSphString					m_sLastError;

	int							m_iMinInfixLen;	///< min indexable infix length (0 means don't index infixes)
	bool						m_bPrefixesOnly;///< whether to index prefixes only or all the infixes

	bool						m_bAttrsUpdated;///< whether in-memory attrs are updated (compared to disk state)
};

/////////////////////////////////////////////////////////////////////////////

/// create phrase fulltext index implemntation
CSphIndex *			sphCreateIndexPhrase ( const char * sFilename );

/// tell libsphinx to be quiet or not (logs and loglevels to come later)
void				sphSetQuiet ( bool bQuiet );

/// creates proper queue for given query
/// modifies pQuery, setups several field locators
/// may return NULL on error; in this case, error message is placed in sError
ISphMatchSorter *	sphCreateQueue ( CSphQuery * pQuery, const CSphSchema & tSchema, CSphString & sError );

/// convert queue to sorted array, and add its entries to result's matches array
void				sphFlattenQueue ( ISphMatchSorter * pQueue, CSphQueryResult * pResult, int iTag );

/////////////////////////////////////////////////////////////////////////////

/// callback type
typedef void		(*SphErrorCallback_fn) ( const char * );

/// register application-level internal error callback
void				sphSetInternalErrorCallback ( SphErrorCallback_fn fnCallback );

#endif // _sphinx_

//
// $Id$
//
