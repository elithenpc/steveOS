#include <efi.h>
#include <efilib.h>
#include <stdint.h>
#include "network.h"

/*
 * Early steveOS networking uses UEFI's HTTP stack. Firmware handles the
 * network adapter, DHCP, IP routing and DNS for this first stage.
 */

static EFI_HTTP_PROTOCOL *http = NULL;
static EFI_HANDLE http_child = NULL;
static EFI_HTTP_SERVICE_BINDING_PROTOCOL *http_binding = NULL;

static EFI_STATUS network_start(void) {
    EFI_STATUS status;

    status = uefi_call_wrapper(BS->LocateProtocol, 3,
                               &gEfiHttpServiceBindingProtocolGuid,
                               NULL, (void **)&http_binding);
    if (EFI_ERROR(status) || !http_binding) return status;

    status = uefi_call_wrapper(http_binding->CreateChild, 2,
                               http_binding, &http_child);
    if (EFI_ERROR(status)) return status;

    status = uefi_call_wrapper(BS->HandleProtocol, 3,
                               http_child, &gEfiHttpProtocolGuid,
                               (void **)&http);
    if (EFI_ERROR(status) || !http) return status;

    EFI_HTTPv4_ACCESS_POINT ipv4;
    SetMem(&ipv4, sizeof(ipv4), 0);
    ipv4.UseDefaultAddress = TRUE;

    EFI_HTTP_CONFIG_DATA config;
    SetMem(&config, sizeof(config), 0);
    config.HttpVersion = HttpVersion11;
    config.TimeOutMillisec = 10000;
    config.LocalAddressIsIPv6 = FALSE;
    config.AccessPoint.IPv4Node = &ipv4;

    status = uefi_call_wrapper(http->Configure, 2, http, &config);
    if (EFI_ERROR(status)) {
        uefi_call_wrapper(http_binding->DestroyChild, 2, http_binding, http_child);
        http = NULL;
        http_child = NULL;
    }
    return status;
}

static void network_stop(void) {
    if (http) {
        uefi_call_wrapper(http->Configure, 2, http, NULL);
        http = NULL;
    }

    if (http_child && http_binding) {
        uefi_call_wrapper(http_binding->DestroyChild, 2,
                          http_binding, http_child);
    }

    http_child = NULL;
    http_binding = NULL;
}

EFI_STATUS steveos_http_test(void) {
    EFI_STATUS status;
    EFI_HTTP_TOKEN request_token;
    EFI_HTTP_TOKEN response_token;
    EFI_HTTP_REQUEST_DATA request_data;
    EFI_HTTP_MESSAGE request_message;
    EFI_HTTP_MESSAGE response_message;
    EFI_HTTP_HEADER request_header;
    CHAR8 url[] = "http://example.com/";
    CHAR8 host[] = "example.com";
    UINT8 response_buffer[1024];

    status = network_start();
    if (EFI_ERROR(status)) return status;

    SetMem(&request_data, sizeof(request_data), 0);
    request_data.Method = HttpMethodGet;
    request_data.Url = url;

    SetMem(&request_header, sizeof(request_header), 0);
    request_header.FieldName = host;
    request_header.FieldValue = host;

    SetMem(&request_message, sizeof(request_message), 0);
    request_message.Data.Request = &request_data;
    request_message.HeaderCount = 1;
    request_message.Headers = &request_header;

    SetMem(&request_token, sizeof(request_token), 0);
    request_token.Message = &request_message;

    status = uefi_call_wrapper(http->Request, 2, http, &request_token);
    if (EFI_ERROR(status)) {
        network_stop();
        return status;
    }

    while (request_token.Status == EFI_NOT_READY)
        uefi_call_wrapper(http->Poll, 2, http);

    if (EFI_ERROR(request_token.Status)) {
        status = request_token.Status;
        network_stop();
        return status;
    }

    SetMem(&response_message, sizeof(response_message), 0);
    response_message.Body = response_buffer;
    response_message.BodyLength = sizeof(response_buffer) - 1;

    SetMem(&response_token, sizeof(response_token), 0);
    response_token.Message = &response_message;

    status = uefi_call_wrapper(http->Response, 2, http, &response_token);
    if (EFI_ERROR(status)) {
        network_stop();
        return status;
    }

    while (response_token.Status == EFI_NOT_READY)
        uefi_call_wrapper(http->Poll, 2, http);

    status = response_token.Status;
    network_stop();
    return status;
}
