#pragma once
/*
 * iLink3 v9 SBE wire format definitions
 *
 * References:
 *   CME iLink 3.0 SBE Message Specification v9
 *   Schema ID: 8, Version: 9
 *
 * Field offsets and block lengths are taken directly from ilinkbinary.xml.
 * All fields are little-endian on the wire.
 */

#include <stdint.h>
#include <string.h>

/* ── SBE framing ─────────────────────────────────────────────────────────── */

/*
 * Every iLink3 SBE message over TCP is preceded by a 4-byte SOFH
 * (2-byte message size + 2-byte encoding type), then the SBE MessageHeader,
 * then the message block, then any variable-length DATA groups.
 *
 * Wire layout per message:
 *   [SOFH 4 bytes][SBE MessageHeader 8 bytes][Block N bytes][DATA groups ...]
 */

/*
 * CME iLink3 TCP framing header – 4 bytes
 *
 * CME uses SOFH: a 2-byte little-endian total frame length (including the
 * SOFH itself) and a 2-byte little-endian encoding type. For SBE
 * little-endian payloads the encoding type is 0xCAFE.
 */
typedef struct __attribute__((packed)) {
    uint16_t message_size;     /* total frame length including this header */
    uint16_t encoding_type;    /* 0xCAFE for SBE little-endian             */
} sofh_t;

/* SBE Message Header – 8 bytes */
typedef struct __attribute__((packed)) {
    uint16_t block_length;   /* root block size, excl. header        */
    uint16_t template_id;    /* message type                         */
    uint16_t schema_id;      /* always 8 for iLink3                  */
    uint16_t version;        /* schema version = 9                   */
} sbe_header_t;

#define ILINK3_SCHEMA_ID      8u
#define ILINK3_SCHEMA_VER     9u
#define ILINK3_ENCODING_TYPE  0xCAFEu

/* ── Template IDs ────────────────────────────────────────────────────────── */

#define TMPL_NEGOTIATE           500u
#define TMPL_NEGOTIATION_RESP    501u
#define TMPL_NEGOTIATION_REJECT  502u
#define TMPL_ESTABLISH           503u
#define TMPL_ESTABLISHMENT_ACK   504u
#define TMPL_ESTABLISHMENT_REJ   505u
#define TMPL_SEQUENCE            506u
#define TMPL_TERMINATE           507u
#define TMPL_RETRANSMIT_REQ      508u
#define TMPL_RETRANSMIT          509u
#define TMPL_NOT_APPLIED         513u

/* ── SBE primitive types ─────────────────────────────────────────────────── */

typedef uint64_t UTCTimestampNanos;   /* ns since Unix epoch              */
typedef uint32_t SeqNum;             /* 1-based sequence number (uInt32) */

/*
 * iLink3 UUID is a 64-bit value (uInt64 in the schema), not a standard
 * 128-bit UUID.  It uniquely identifies a negotiation session.
 */
typedef uint64_t ilink3_uuid_t;

/* Null sentinels for optional (nullable) fields */
#define SEQNUM_NULL  UINT32_MAX
#define TS_NULL      UINT64_MAX
#define U16_NULL     UINT16_MAX
#define U8_NULL      UINT8_MAX

/* ── Negotiate (500) ─────────────────────────────────────────────────────── */
/*
 * Client → CME.  First message of a new session.
 * blockLength = 76
 *
 * Constant fields (not encoded in block):
 *   CustomerFlow  = "IDEMPOTENT"    (ClientFlowType)
 *   HMACVersion   = "CME-1-SHA-256" (HMACVersion)
 *
 * After the block, one DATA group is appended:
 *   Credentials: uint16_t numBytes followed by numBytes of raw credential data.
 *   Send numBytes=0 if not used.
 */
typedef struct __attribute__((packed)) {
    char         hmac_signature[32];  /* @ 0:  HMACSignature  String32Req  */
    char         access_key_id[20];   /* @ 32: AccessKeyID    String20Req  */
    ilink3_uuid_t uuid;               /* @ 52: UUID           uInt64       */
    uint64_t     request_timestamp;   /* @ 60: RequestTimestamp uInt64     */
    char         session[3];          /* @ 68: Session        String3Req   */
    char         firm[5];             /* @ 71: Firm           String5Req   */
} negotiate_body_t;                   /* 76 bytes */

#define NEGOTIATE_BLOCK_LEN  76u

/* ── NegotiationResponse (501) ───────────────────────────────────────────── */
/*
 * CME → Client.  Acknowledges Negotiate.
 * blockLength = 33
 *
 * Constant fields: ServerFlow = "RECOVERABLE"
 * After the block: Credentials DATA group (optional).
 */
typedef struct __attribute__((packed)) {
    ilink3_uuid_t uuid;               /* @ 0:  UUID                        uInt64      */
    uint64_t      request_timestamp;  /* @ 8:  RequestTimestamp            uInt64      */
    uint16_t      secret_key_secure_id_exp; /* @ 16: SecretKeySecureIDExpiration uInt16NULL */
    uint8_t       fault_tolerance_ind;/* @ 18: FaultToleranceIndicator     FTI/uInt8NULL */
    uint8_t       split_msg;          /* @ 19: SplitMsg                    uInt8NULL   */
    uint32_t      prev_seq_no;        /* @ 20: PreviousSeqNo               uInt32      */
    ilink3_uuid_t prev_uuid;          /* @ 24: PreviousUUID                uInt64      */
    uint8_t       environment_indicator; /* @ 32: EnvironmentIndicator     uInt8NULL   */
} negotiation_resp_body_t;            /* 33 bytes */

#define NEGOTIATION_RESP_BLOCK_LEN  33u

/* ── NegotiationReject (502) ─────────────────────────────────────────────── */
/*
 * blockLength = 69
 */
typedef struct __attribute__((packed)) {
    char          reason[48];         /* @ 0:  Reason                      String48    */
    ilink3_uuid_t uuid;               /* @ 48: UUID                        uInt64      */
    uint64_t      request_timestamp;  /* @ 56: RequestTimestamp            uInt64      */
    uint16_t      error_codes;        /* @ 64: ErrorCodes                  uInt16      */
    uint8_t       fault_tolerance_ind;/* @ 66: FaultToleranceIndicator     FTI/uInt8NULL */
    uint8_t       split_msg;          /* @ 67: SplitMsg                    uInt8NULL   */
    uint8_t       environment_indicator; /* @ 68: EnvironmentIndicator     uInt8NULL   */
} negotiation_reject_body_t;          /* 69 bytes */

/* ── Establish (503) ─────────────────────────────────────────────────────── */
/*
 * Client → CME.  Sent after successful NegotiationResponse.
 * blockLength = 132
 *
 * Constant fields: HMACVersion = "CME-1-SHA-256"
 * After the block: Credentials DATA group (optional).
 */
typedef struct __attribute__((packed)) {
    char         hmac_signature[32];      /* @ 0:   HMACSignature         String32Req  */
    char         access_key_id[20];       /* @ 32:  AccessKeyID           String20Req  */
    char         trading_system_name[30]; /* @ 52:  TradingSystemName     String30Req  */
    char         trading_system_version[10]; /* @ 82: TradingSystemVersion String10Req */
    char         trading_system_vendor[10];  /* @ 92: TradingSystemVendor  String10Req */
    ilink3_uuid_t uuid;                   /* @ 102: UUID                  uInt64       */
    uint64_t     request_timestamp;       /* @ 110: RequestTimestamp      uInt64       */
    uint32_t     next_seq_no;             /* @ 118: NextSeqNo             uInt32       */
    char         session[3];              /* @ 122: Session               String3Req   */
    char         firm[5];                 /* @ 125: Firm                  String5Req   */
    uint16_t     keep_alive_interval;     /* @ 130: KeepAliveInterval     uInt16       */
} establish_body_t;                       /* 132 bytes */

#define ESTABLISH_BLOCK_LEN  132u

/* ── EstablishmentAck (504) ──────────────────────────────────────────────── */
/*
 * blockLength = 39
 */
typedef struct __attribute__((packed)) {
    ilink3_uuid_t uuid;               /* @ 0:  UUID                        uInt64      */
    uint64_t      request_timestamp;  /* @ 8:  RequestTimestamp            uInt64      */
    uint32_t      next_seq_no;        /* @ 16: NextSeqNo                   uInt32      */
    uint32_t      prev_seq_no;        /* @ 20: PreviousSeqNo               uInt32      */
    ilink3_uuid_t prev_uuid;          /* @ 24: PreviousUUID                uInt64      */
    uint16_t      keep_alive_interval;/* @ 32: KeepAliveInterval           uInt16      */
    uint16_t      secret_key_secure_id_exp; /* @ 34: SecretKeySecureIDExpiration uInt16NULL */
    uint8_t       fault_tolerance_ind;/* @ 36: FaultToleranceIndicator     FTI/uInt8NULL */
    uint8_t       split_msg;          /* @ 37: SplitMsg                    uInt8NULL   */
    uint8_t       environment_indicator; /* @ 38: EnvironmentIndicator     uInt8NULL   */
} establishment_ack_body_t;           /* 39 bytes */

#define ESTABLISHMENT_ACK_BLOCK_LEN  39u

/* ── EstablishmentReject (505) ───────────────────────────────────────────── */
/*
 * blockLength = 73
 */
typedef struct __attribute__((packed)) {
    char          reason[48];         /* @ 0:  Reason                      String48    */
    ilink3_uuid_t uuid;               /* @ 48: UUID                        uInt64      */
    uint64_t      request_timestamp;  /* @ 56: RequestTimestamp            uInt64      */
    uint32_t      next_seq_no;        /* @ 64: NextSeqNo                   uInt32      */
    uint16_t      error_codes;        /* @ 68: ErrorCodes                  uInt16      */
    uint8_t       fault_tolerance_ind;/* @ 70: FaultToleranceIndicator     FTI/uInt8NULL */
    uint8_t       split_msg;          /* @ 71: SplitMsg                    uInt8NULL   */
    uint8_t       environment_indicator; /* @ 72: EnvironmentIndicator     uInt8NULL   */
} establishment_rej_body_t;           /* 73 bytes */

/* ── Sequence (506) ──────────────────────────────────────────────────────── */
/*
 * Keepalive / heartbeat.  Sent by both sides.
 * blockLength = 14
 */
typedef struct __attribute__((packed)) {
    ilink3_uuid_t uuid;               /* @ 0:  UUID                        uInt64      */
    uint32_t      next_seq_no;        /* @ 8:  NextSeqNo                   uInt32      */
    uint8_t       fault_tolerance_ind;/* @ 12: FaultToleranceIndicator     FTI/uInt8NULL */
    uint8_t       keep_alive_interval_lapsed; /* @ 13: KeepAliveIntervalLapsed uInt8  */
} sequence_body_t;                    /* 14 bytes */

#define SEQUENCE_BLOCK_LEN  14u

/* ── Terminate (507) ─────────────────────────────────────────────────────── */
/*
 * Either side may send. No ACK expected – close TCP after sending.
 * blockLength = 67
 */
typedef struct __attribute__((packed)) {
    char          reason[48];         /* @ 0:  Reason                      String48    */
    ilink3_uuid_t uuid;               /* @ 48: UUID                        uInt64      */
    uint64_t      request_timestamp;  /* @ 56: RequestTimestamp            uInt64      */
    uint16_t      error_codes;        /* @ 64: ErrorCodes                  uInt16      */
    uint8_t       split_msg;          /* @ 66: SplitMsg                    uInt8NULL   */
} terminate_body_t;                   /* 67 bytes */

#define TERMINATE_BLOCK_LEN  67u

/* ── Wire frame helpers ──────────────────────────────────────────────────── */

static inline size_t ilink3_frame_size(size_t body_len)
{
    return sizeof(sofh_t) + sizeof(sbe_header_t) + body_len;
}

/*
 * Write SOFH + SBE header into buf, return pointer just past headers so
 * caller can memcpy body (block + any DATA groups).
 */
static inline uint8_t *ilink3_write_headers(uint8_t *buf,
                                            uint16_t template_id,
                                            uint16_t block_length,
                                            size_t   total_frame_len)
{
    sofh_t *sofh = (sofh_t *)buf;
    sofh->message_size  = (uint16_t)total_frame_len;
    sofh->encoding_type = ILINK3_ENCODING_TYPE;

    sbe_header_t *hdr = (sbe_header_t *)(buf + sizeof(sofh_t));
    hdr->block_length = block_length;
    hdr->template_id  = template_id;
    hdr->schema_id    = ILINK3_SCHEMA_ID;
    hdr->version      = ILINK3_SCHEMA_VER;

    return buf + sizeof(sofh_t) + sizeof(sbe_header_t);
}

/* Parse the SBE header from a received buffer; returns template_id or 0 */
static inline uint16_t ilink3_parse_template(const uint8_t *buf, size_t len)
{
    if (len < sizeof(sofh_t) + sizeof(sbe_header_t))
        return 0;
    const sofh_t *sofh = (const sofh_t *)buf;
    if (sofh->encoding_type != ILINK3_ENCODING_TYPE)
        return 0;
    if (sofh->message_size > len)
        return 0;
    const sbe_header_t *hdr = (const sbe_header_t *)(buf + sizeof(sofh_t));
    if (hdr->schema_id != ILINK3_SCHEMA_ID)
        return 0;
    return hdr->template_id;
}
