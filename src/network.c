#include <efi.h>
#include <efilib.h>
#include "network.h"

/* UEFI HTTP protocol GUIDs from the UEFI specification. */
static EFI_GUID http_service_binding_guid =
    {0xbdc8e6af, 0xd9bc, 0x4379, {0xa7, 0x2a, 0xe0, 0xc4, 0xe7, 0x5d, 0xae, 0x1c}};
static EFI_GUID http_protocol_guid =
    {0x7a59b29b, 0x910b, 0x4171, {0x82, 0x42, 0xa8, 0x5a, 0x0d, 0xf2, 0x5b, 0x5b}};

typedef struct _STEVEOS_SERVICE_BINDING STEVEOS_SERVICE_BINDING;
typedef EFI_STATUS (EFIAPI *STEVEOS_CREATE_CHILD)(STEVEOS_SERVICE_BINDING *This, EFI_HANDLE *ChildHandle);
typedef EFI_STATUS (EFIAPI *STEVEOS_DESTROY_CHILD)(STEVEOS_SERVICE_BINDING *This, EFI_HANDLE ChildHandle);
struct _STEVEOS_SERVICE_BINDING {
    STEVEOS_CREATE_CHILD CreateChild;
    STEVEOS_DESTROY_CHILD DestroyChild;
};

typedef enum {
    HttpVersion10,
    HttpVersion11,
    HttpVersionUnsupported
} STEVEOS_HTTP_VERSION;

typedef enum {
    HttpMethodGet,
    HttpMethodPost,
    HttpMethodPatch,
    HttpMethodOptions,
    HttpMethodConnect,
    HttpMethodHead,
    HttpMethodPut,
    HttpMethodDelete,
    HttpMethodTrace,
    HttpMethodMax
} STEVEOS_HTTP_METHOD;

typedef struct {
    UINT8 Addr[4];
} STEVEOS_IPV4_ADDRESS;

typedef struct {
    BOOLEAN UseDefaultAddress;
    STEVEOS_IPV4_ADDRESS LocalAddress;
    STEVEOS_IPV4_ADDRESS LocalSubnet;
    UINT16 LocalPort;
} STEVEOS_HTTPV4_ACCESS_POINT;

typedef struct {
    STEVEOS_HTTP_VERSION HttpVersion;
    UINT32 TimeOutMillisec;
    BOOLEAN LocalAddressIsIPv6;
    union {
        STEVEOS_HTTPV4_ACCESS_POINT *IPv4Node;
        VOID *IPv6Node;
    } AccessPoint;
} STEVEOS_HTTP_CONFIG_DATA;

typedef struct {
    STEVEOS_HTTP_METHOD Method;
    CHAR16 *Url;
} STEVEOS_HTTP_REQUEST_DATA;

typedef struct {
    UINT32 StatusCode;
} STEVEOS_HTTP_RESPONSE_DATA;

typedef struct {
    CHAR8 *FieldName;
    CHAR8 *FieldValue;
} STEVEOS_HTTP_HEADER;

typedef struct {
    union {
        STEVEOS_HTTP_REQUEST_DATA *Request;
        STEVEOS_HTTP_RESPONSE_DATA *Response;
    } Data;
    UINTN HeaderCount;
    STEVEOS_HTTP_HEADER *Headers;
    UINTN BodyLength;
    VOID *Body;
} STEVEOS_HTTP_MESSAGE;

typedef struct {
    EFI_EVENT Event;
    EFI_STATUS Status;
    STEVEOS_HTTP_MESSAGE *Message;
} STEVEOS_HTTP_TOKEN;

typedef EFI_STATUS (EFIAPI *STEVEOS_HTTP_CONFIGURE)(VOID *This, STEVEOS_HTTP_CONFIG_DATA *ConfigData);
typedef EFI_STATUS (EFIAPI *STEVEOS_HTTP_REQUEST)(VOID *This, STEVEOS_HTTP_TOKEN *Token);
typedef EFI_STATUS (EFIAPI *STEVEOS_HTTP_RESPONSE)(VOID *This, STEVEOS_HTTP_TOKEN *Token);
typedef EFI_STATUS (EFIAPI *STEVEOS_HTTP_POLL)(VOID *This);

typedef struct {
    VOID *GetModeData;
    STEVEOS_HTTP_CONFIGURE Configure;
    STEVEOS_HTTP_REQUEST Request;
    VOID *Cancel;
    STEVEOS_HTTP_RESPONSE Response;
    STEVEOS_HTTP_POLL Poll;
} STEVEOS_HTTP_PROTOCOL;

static volatile BOOLEAN request_done;
static volatile BOOLEAN response_done;

static VOID EFIAPI request_notify(EFI_EVENT Event, VOID *Context) {
    (void)Event;
    (void)Context;
    request_done = TRUE;
}

static VOID EFIAPI response_notify(EFI_EVENT Event, VOID *Context) {
    (void)Event;
    (void)Context;
    response_done = TRUE;
}

EFI_STATUS steveos_network_available(void) {
    /* Never probe networking during the early boot path. */
    return EFI_UNSUPPORTED;
}

EFI_STATUS steveos_http_test(void) {
    STEVEOS_SERVICE_BINDING *binding = NULL;
    EFI_STATUS st = uefi_call_wrapper(BS->LocateProtocol, 3,
                                      &http_service_binding_guid,
                                      NULL, (VOID **)&binding);
    return EFI_ERROR(st) ? st : EFI_SUCCESS;
}

static EFI_STATUS steveos_http_get_internal(const CHAR16 *url,
                                              CHAR8 *out,
                                              UINTN out_capacity,
                                              UINTN *out_length,
                                              UINT32 *http_status,
                                              UINTN redirects) {
    if (!url || !out || out_capacity < 2 || !out_length)
        return EFI_INVALID_PARAMETER;

    *out_length = 0;
    out[0] = 0;
    if (http_status)
        *http_status = 0;
    if (redirects > 4)
        return EFI_ABORTED;

    STEVEOS_SERVICE_BINDING *binding = NULL;
    EFI_HANDLE child = NULL;
    STEVEOS_HTTP_PROTOCOL *http = NULL;
    EFI_EVENT request_event = NULL;
    EFI_EVENT response_event = NULL;
    EFI_STATUS st;

    st = uefi_call_wrapper(BS->LocateProtocol, 3,
                           &http_service_binding_guid,
                           NULL, (VOID **)&binding);
    if (EFI_ERROR(st) || !binding)
        return EFI_UNSUPPORTED;

    st = uefi_call_wrapper(binding->CreateChild, 2, binding, &child);
    if (EFI_ERROR(st))
        return st;

    st = uefi_call_wrapper(BS->HandleProtocol, 3,
                           child, &http_protocol_guid, (VOID **)&http);
    if (EFI_ERROR(st) || !http) {
        uefi_call_wrapper(binding->DestroyChild, 2, binding, child);
        return EFI_UNSUPPORTED;
    }

    STEVEOS_HTTPV4_ACCESS_POINT ipv4;
    STEVEOS_HTTP_CONFIG_DATA config;
    ZeroMem(&ipv4, sizeof(ipv4));
    ZeroMem(&config, sizeof(config));
    ipv4.UseDefaultAddress = TRUE;
    config.HttpVersion = HttpVersion11;
    config.TimeOutMillisec = 8000;
    config.LocalAddressIsIPv6 = FALSE;
    config.AccessPoint.IPv4Node = &ipv4;

    st = uefi_call_wrapper(http->Configure, 2, http, &config);
    if (EFI_ERROR(st))
        goto cleanup;

    STEVEOS_HTTP_REQUEST_DATA req_data;
    STEVEOS_HTTP_MESSAGE req_msg;
    STEVEOS_HTTP_TOKEN req_token;
    ZeroMem(&req_data, sizeof(req_data));
    ZeroMem(&req_msg, sizeof(req_msg));
    ZeroMem(&req_token, sizeof(req_token));
    req_data.Method = HttpMethodGet;
    req_data.Url = (CHAR16 *)url;
    req_msg.Data.Request = &req_data;
    req_token.Message = &req_msg;
    req_token.Status = EFI_NOT_READY;

    st = uefi_call_wrapper(BS->CreateEvent, 5,
                           EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
                           request_notify, NULL, &request_event);
    if (EFI_ERROR(st))
        goto cleanup;
    req_token.Event = request_event;
    request_done = FALSE;

    st = uefi_call_wrapper(http->Request, 2, http, &req_token);
    if (EFI_ERROR(st))
        goto cleanup;

    for (UINTN waited = 0; !request_done && waited < 300000; ++waited) {
        if (http->Poll)
            uefi_call_wrapper(http->Poll, 1, http);
        uefi_call_wrapper(BS->Stall, 1, 1000);
    }
    if (!request_done) {
        st = EFI_TIMEOUT;
        goto cleanup;
    }
    if (EFI_ERROR(req_token.Status)) {
        st = req_token.Status;
        goto cleanup;
    }

    STEVEOS_HTTP_RESPONSE_DATA resp_data;
    STEVEOS_HTTP_MESSAGE resp_msg;
    STEVEOS_HTTP_TOKEN resp_token;
    ZeroMem(&resp_data, sizeof(resp_data));
    ZeroMem(&resp_msg, sizeof(resp_msg));
    ZeroMem(&resp_token, sizeof(resp_token));
    resp_msg.Data.Response = &resp_data;
    resp_msg.BodyLength = out_capacity - 1;
    resp_msg.Body = out;
    resp_token.Message = &resp_msg;
    resp_token.Status = EFI_NOT_READY;

    st = uefi_call_wrapper(BS->CreateEvent, 5,
                           EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
                           response_notify, NULL, &response_event);
    if (EFI_ERROR(st))
        goto cleanup;
    resp_token.Event = response_event;
    response_done = FALSE;

    st = uefi_call_wrapper(http->Response, 2, http, &resp_token);
    if (EFI_ERROR(st))
        goto cleanup;

    for (UINTN waited = 0; !response_done && waited < 10000; ++waited) {
        if (http->Poll)
            uefi_call_wrapper(http->Poll, 1, http);
        uefi_call_wrapper(BS->Stall, 1, 1000);
    }
    if (!response_done) {
        st = EFI_TIMEOUT;
        goto cleanup;
    }
    st = resp_token.Status;
    if (http_status)
        *http_status = resp_data.StatusCode;
    if (!EFI_ERROR(st)) {
        UINTN n = resp_msg.BodyLength;
        if (n >= out_capacity)
            n = out_capacity - 1;

        if (resp_data.StatusCode >= 300 && resp_data.StatusCode < 400 &&
            resp_msg.HeaderCount && resp_msg.Headers) {
            CHAR16 redirect[1024];
            UINTN redirect_len = 0;
            BOOLEAN found = FALSE;
            ZeroMem(redirect, sizeof(redirect));
            for (UINTN h = 0; h < resp_msg.HeaderCount; ++h) {
                CHAR8 *name = resp_msg.Headers[h].FieldName;
                CHAR8 *value = resp_msg.Headers[h].FieldValue;
                if (!name || !value)
                    continue;
                if ((name[0]=='L'||name[0]=='l') && (name[1]=='O'||name[1]=='o') &&
                    (name[2]=='C'||name[2]=='c') && (name[3]=='A'||name[3]=='a') &&
                    (name[4]=='T'||name[4]=='t') && (name[5]=='I'||name[5]=='i') &&
                    (name[6]=='O'||name[6]=='o') && (name[7]=='N'||name[7]=='n') &&
                    name[8]==0) {
                    while (value[redirect_len] && redirect_len + 1 < 1024) {
                        redirect[redirect_len] = (CHAR16)(UINT8)value[redirect_len];
                        redirect_len++;
                    }
                    redirect[redirect_len] = 0;
                    found = redirect_len > 0;
                    break;
                }
            }

            if (found) {
                EFI_EVENT old_response_event = response_event;
                EFI_EVENT old_request_event = request_event;
                response_event = NULL;
                request_event = NULL;
                if (old_response_event)
                    uefi_call_wrapper(BS->CloseEvent, 1, old_response_event);
                if (old_request_event)
                    uefi_call_wrapper(BS->CloseEvent, 1, old_request_event);
                if (http)
                    uefi_call_wrapper(http->Configure, 2, http, NULL);
                if (binding && child)
                    uefi_call_wrapper(binding->DestroyChild, 2, binding, child);
                return steveos_http_get_internal(redirect, out, out_capacity,
                                                  out_length, http_status,
                                                  redirects + 1);
            }

        out[n] = 0;
        *out_length = n;
        }
    }

cleanup:
    if (response_event)
        uefi_call_wrapper(BS->CloseEvent, 1, response_event);
    if (request_event)
        uefi_call_wrapper(BS->CloseEvent, 1, request_event);
    if (http)
        uefi_call_wrapper(http->Configure, 2, http, NULL);
    if (binding && child)
        uefi_call_wrapper(binding->DestroyChild, 2, binding, child);
    return st;
}

EFI_STATUS steveos_http_get(const CHAR16 *url,
                            CHAR8 *out,
                            UINTN out_capacity,
                            UINTN *out_length,
                            UINT32 *http_status) {
    return steveos_http_get_internal(url, out, out_capacity,
                                      out_length, http_status, 0);
}
