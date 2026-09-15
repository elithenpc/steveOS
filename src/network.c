#include <efi.h>
#include <efilib.h>
#include <stdint.h>

/*
 * Early steveOS networking uses the firmware's UEFI HTTP stack.
 * This gives us real network access without dragging a full TCP/IP stack
 * into the first bootable build. Later the OS can replace this with its own
 * Ethernet/Wi-Fi, IP, DNS, TCP and TLS drivers.
 */

static EFI_HTTP_PROTOCOL *http = NULL;
static EFI_HANDLE http_child = NULL;

static EFI_STATUS network_start(void) {
    EFI_STATUS status;
    EFI_HTTP_SERVICE_BINDING_PROTOCOL *binding = NULL;

    status = uefi_call_wrapper(BS->LocateProtocol, 3,
                               &gEfiHttpServiceBindingProtocolGuid,
                               NULL, (void **)&binding);
    if (EFI_ERROR(status) || !binding) return status;

    status = uefi_call_wrapper(binding->CreateChild, 2, binding, &http_child);
    if (EFI_ERROR(status)) return status;

    status = uefi_call_wrapper(BS->HandleProtocol, 3,
                               http_child, &gEfiHttpProtocolGuid,
                               (void **)&http);
    if (EFI_ERROR(status) || !http) return status;

    EFI_HTTP_CONFIG_DATA config;
    SetMem(&config, sizeof(config), 0);
    config.HttpVersion = HttpVersion11;
    config.TimeOutMillisec = 10000;
    config.LocalAddressIsIPv6 = FALSE;
    config.AccessPoint.IPv4Node = NULL;

    return uefi_call_wrapper(http->Configure, 2, http, &config);
}

static void network_stop(void) {
    if (http) {
        uefi_call_wrapper(http->Configure, 2, http, NULL);
        http = NULL;
    }

    if (http_child) {
        EFI_HTTP_SERVICE_BINDING_PROTOCOL *binding = NULL;
        if (!EFI_ERROR(uefi_call_wrapper(BS->LocateProtocol, 3,
                                         &gEfiHttpServiceBindingProtocolGuid,
                                         NULL, (void **)&binding)) && binding) {
            uefi_call_wrapper(binding->DestroyChild, 2, binding, http_child);
        }
        http_child = NULL;
    }
}

/*
 * Fetch a small HTTP page to prove the machine can reach the internet.
 * Returns EFI_SUCCESS when the HTTP transaction completes successfully.
 */
EFI_STATUS steveos_http_test(void) {
    EFI_STATUS status;
    EFI_HTTP_TOKEN request_token;
    EFI_HTTP_TOKEN response_token;
    EFI_HTTP_REQUEST_DATA request_data;
    EFI_HTTP_MESSAGE request_message;
    EFI_HTTP_MESSAGE response_message;
    EFI_HTTP_HEADER request_header;
    EFI_HTTP_HEADER *response_headers = NULL;
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

    while (request_token.Status == EFI_NOT_READY) {
        uefi_call_wrapper(http->Poll, 2, http);
    }
    if (EFI_ERROR(request_token.Status)) {
        network_stop();
        return request_token.Status;
    }

    SetMem(&response_message, sizeof(response_message), 0);
    response_message.Headers = response_headers;
    response_message.Body = response_buffer;
    response_message.BodyLength = sizeof(response_buffer) - 1;

    SetMem(&response_token, sizeof(response_token), 0);
    response_token.Message = &response_message;

    status = uefi_call_wrapper(http->Response, 2, http, &response_token);
    if (EFI_ERROR(status)) {
        network_stop();
        return status;
    }

    while (response_token.Status == EFI_NOT_READY) {
        uefi_call_wrapper(http->Poll, 2, http);
    }

    status = response_token.Status;
    network_stop();
    return status;
}
