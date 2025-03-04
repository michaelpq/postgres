/*-------------------------------------------------------------------------
 *
 * hmac_openssl.c
 *	  Implementation of HMAC with OpenSSL.
 *
 * This should only be used if code is compiled with OpenSSL support.
 *
 * Portions Copyright (c) 1996-2025, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  src/common/hmac_openssl.c
 *
 *-------------------------------------------------------------------------
 */

#ifndef FRONTEND
#include "postgres.h"
#else
#include "postgres_fe.h"
#endif


#include <openssl/err.h>
#include <openssl/hmac.h>

#include "common/hmac.h"
#include "common/md5.h"
#include "common/sha1.h"
#include "common/sha2.h"
#ifndef FRONTEND
#include "utils/memutils.h"
#include "utils/resowner.h"
#endif

/*
 * In backend, use an allocation in TopMemoryContext to count for resowner
 * cleanup handling if necessary.  In frontend, use malloc to be able to return
 * a failure status back to the caller.
 */
#ifndef FRONTEND
#define USE_RESOWNER_FOR_HMAC
#define ALLOC(size) MemoryContextAlloc(TopMemoryContext, size)
#define FREE(ptr) pfree(ptr)
#else							/* FRONTEND */
#define ALLOC(size) malloc(size)
#define FREE(ptr) free(ptr)
#endif							/* FRONTEND */

/* Set of error states */
typedef enum pg_hmac_errno
{
	PG_HMAC_ERROR_NONE = 0,
	PG_HMAC_ERROR_DEST_LEN,
	PG_HMAC_ERROR_OPENSSL,
} pg_hmac_errno;

/* Internal pg_hmac_ctx structure */
struct pg_hmac_ctx
{
	HMAC_CTX   *hmacctx;
	pg_cryptohash_type type;
	pg_hmac_errno error;
	const char *errreason;

#ifdef USE_RESOWNER_FOR_HMAC
	ResourceOwner resowner;
#endif
};

/* ResourceOwner callbacks to hold HMAC contexts */
#ifdef USE_RESOWNER_FOR_HMAC
static void ResOwnerReleaseHMAC(Datum res);

static const ResourceOwnerDesc hmac_resowner_desc =
{
	.name = "OpenSSL HMAC context",
	.release_phase = RESOURCE_RELEASE_BEFORE_LOCKS,
	.release_priority = RELEASE_PRIO_HMAC_CONTEXTS,
	.ReleaseResource = ResOwnerReleaseHMAC,
	.DebugPrint = NULL			/* the default message is fine */
};

/* Convenience wrappers over ResourceOwnerRemember/Forget */
static inline void
ResourceOwnerRememberHMAC(ResourceOwner owner, pg_hmac_ctx *ctx)
{
	ResourceOwnerRemember(owner, PointerGetDatum(ctx), &hmac_resowner_desc);
}
static inline void
ResourceOwnerForgetHMAC(ResourceOwner owner, pg_hmac_ctx *ctx)
{
	ResourceOwnerForget(owner, PointerGetDatum(ctx), &hmac_resowner_desc);
}
#endif

static const char *
SSLerrmessage(unsigned long ecode)
{
	if (ecode == 0)
		return NULL;

	/*
	 * This may return NULL, but we would fall back to a default error path if
	 * that were the case.
	 */
	return ERR_reason_error_string(ecode);
}

/*
 * pg_hmac_create
 *
 * Allocate a hash context.  Returns NULL on failure for an OOM.  The
 * backend issues an error, without returning.
 */
pg_hmac_ctx *
pg_hmac_create(pg_cryptohash_type type)
{
	pg_hmac_ctx *ctx;

	ctx = ALLOC(sizeof(pg_hmac_ctx));
	if (ctx == NULL)
		return NULL;
	memset(ctx, 0, sizeof(pg_hmac_ctx));

	ctx->type = type;
	ctx->error = PG_HMAC_ERROR_NONE;
	ctx->errreason = NULL;


	/*
	 * Initialization takes care of assigning the correct type for OpenSSL.
	 * Also ensure that there aren't any unconsumed errors in the queue from
	 * previous runs.
	 */
	ERR_clear_error();

#ifdef USE_RESOWNER_FOR_HMAC
	ResourceOwnerEnlarge(CurrentResourceOwner);
#endif

	ctx->hmacctx = HMAC_CTX_new();

	if (ctx->hmacctx == NULL)
	{
		explicit_bzero(ctx, sizeof(pg_hmac_ctx));
		FREE(ctx);
#ifndef FRONTEND
		ereport(ERROR,
				(errcode(ERRCODE_OUT_OF_MEMORY),
				 errmsg("out of memory")));
#endif
		return NULL;
	}


#ifdef USE_RESOWNER_FOR_HMAC
	ctx->resowner = CurrentResourceOwner;
	ResourceOwnerRememberHMAC(CurrentResourceOwner, ctx);
#endif

	return ctx;
}

/*
 * pg_hmac_init
 *
 * Initialize a HMAC context.  Returns 0 on success, -1 on failure.
 */
int
pg_hmac_init(pg_hmac_ctx *ctx, const uint8 *key, size_t len)
{
	int			status = 0;

	if (ctx == NULL)
		return -1;

	switch (ctx->type)
	{
		case PG_MD5:
			status = HMAC_Init_ex(ctx->hmacctx, key, len, EVP_md5(), NULL);
			break;
		case PG_SHA1:
			status = HMAC_Init_ex(ctx->hmacctx, key, len, EVP_sha1(), NULL);
			break;
		case PG_SHA224:
			status = HMAC_Init_ex(ctx->hmacctx, key, len, EVP_sha224(), NULL);
			break;
		case PG_SHA256:
			status = HMAC_Init_ex(ctx->hmacctx, key, len, EVP_sha256(), NULL);
			break;
		case PG_SHA384:
			status = HMAC_Init_ex(ctx->hmacctx, key, len, EVP_sha384(), NULL);
			break;
		case PG_SHA512:
			status = HMAC_Init_ex(ctx->hmacctx, key, len, EVP_sha512(), NULL);
			break;
	}

	/* OpenSSL internals return 1 on success, 0 on failure */
	if (status <= 0)
	{
		ctx->errreason = SSLerrmessage(ERR_get_error());
		ctx->error = PG_HMAC_ERROR_OPENSSL;
		return -1;
	}

	return 0;
}

/*
 * pg_hmac_update
 *
 * Update a HMAC context.  Returns 0 on success, -1 on failure.
 */
int
pg_hmac_update(pg_hmac_ctx *ctx, const uint8 *data, size_t len)
{
	int			status = 0;

	if (ctx == NULL)
		return -1;

	status = HMAC_Update(ctx->hmacctx, data, len);

	/* OpenSSL internals return 1 on success, 0 on failure */
	if (status <= 0)
	{
		ctx->errreason = SSLerrmessage(ERR_get_error());
		ctx->error = PG_HMAC_ERROR_OPENSSL;
		return -1;
	}
	return 0;
}

/*
 * pg_hmac_final
 *
 * Finalize a HMAC context.  Returns 0 on success, -1 on failure.
 */
int
pg_hmac_final(pg_hmac_ctx *ctx, uint8 *dest, size_t len)
{
	int			status = 0;
	uint32		outlen;

	if (ctx == NULL)
		return -1;

	switch (ctx->type)
	{
		case PG_MD5:
			if (len < MD5_DIGEST_LENGTH)
			{
				ctx->error = PG_HMAC_ERROR_DEST_LEN;
				return -1;
			}
			break;
		case PG_SHA1:
			if (len < SHA1_DIGEST_LENGTH)
			{
				ctx->error = PG_HMAC_ERROR_DEST_LEN;
				return -1;
			}
			break;
		case PG_SHA224:
			if (len < PG_SHA224_DIGEST_LENGTH)
			{
				ctx->error = PG_HMAC_ERROR_DEST_LEN;
				return -1;
			}
			break;
		case PG_SHA256:
			if (len < PG_SHA256_DIGEST_LENGTH)
			{
				ctx->error = PG_HMAC_ERROR_DEST_LEN;
				return -1;
			}
			break;
		case PG_SHA384:
			if (len < PG_SHA384_DIGEST_LENGTH)
			{
				ctx->error = PG_HMAC_ERROR_DEST_LEN;
				return -1;
			}
			break;
		case PG_SHA512:
			if (len < PG_SHA512_DIGEST_LENGTH)
			{
				ctx->error = PG_HMAC_ERROR_DEST_LEN;
				return -1;
			}
			break;
	}

	status = HMAC_Final(ctx->hmacctx, dest, &outlen);

	/* OpenSSL internals return 1 on success, 0 on failure */
	if (status <= 0)
	{
		ctx->errreason = SSLerrmessage(ERR_get_error());
		ctx->error = PG_HMAC_ERROR_OPENSSL;
		return -1;
	}
	return 0;
}

/*
 * pg_hmac_free
 *
 * Free a HMAC context.
 */
void
pg_hmac_free(pg_hmac_ctx *ctx)
{
	if (ctx == NULL)
		return;

	HMAC_CTX_free(ctx->hmacctx);
#ifdef USE_RESOWNER_FOR_HMAC
	if (ctx->resowner)
		ResourceOwnerForgetHMAC(ctx->resowner, ctx);
#endif

	explicit_bzero(ctx, sizeof(pg_hmac_ctx));
	FREE(ctx);
}

/*
 * pg_hmac_error
 *
 * Returns a static string providing details about an error that happened
 * during a HMAC computation.
 */
const char *
pg_hmac_error(pg_hmac_ctx *ctx)
{
	if (ctx == NULL)
		return _("out of memory");

	/*
	 * If a reason is provided, rely on it, else fallback to any error code
	 * set.
	 */
	if (ctx->errreason)
		return ctx->errreason;

	switch (ctx->error)
	{
		case PG_HMAC_ERROR_NONE:
			return _("success");
		case PG_HMAC_ERROR_DEST_LEN:
			return _("destination buffer too small");
		case PG_HMAC_ERROR_OPENSSL:
			return _("OpenSSL failure");
	}

	Assert(false);				/* cannot be reached */
	return _("success");
}

/* ResourceOwner callbacks */

#ifdef USE_RESOWNER_FOR_HMAC
static void
ResOwnerReleaseHMAC(Datum res)
{
	pg_hmac_ctx *ctx = (pg_hmac_ctx *) DatumGetPointer(res);

	ctx->resowner = NULL;
	pg_hmac_free(ctx);
}
#endif
