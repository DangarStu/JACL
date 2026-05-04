/* auth_stub.c --- No-op stub used when fcgijacl is built without
 * libcurl / jansson / openssl. Every auth_* entry point reports
 * "auth disabled" so cgijacl falls through to the anonymous flow.
 */

#include "auth.h"
#include <string.h>

void
auth_configure(const char *google_client_id,
               const char *session_secret,
               long session_max_age_seconds)
{
    (void) google_client_id;
    (void) session_secret;
    (void) session_max_age_seconds;
}

int
auth_is_enabled(void)
{
    return 0;
}

const char *
auth_google_client_id(void)
{
    return "";
}

long
auth_session_max_age(void)
{
    return 0;
}

int
auth_verify_id_token(const char *id_token,
                     char *sub_out, size_t sub_size,
                     char *err_out, size_t err_size)
{
    (void) id_token;
    if (sub_out && sub_size > 0) sub_out[0] = 0;
    if (err_out && err_size > 0) {
        strncpy(err_out, "auth not built in", err_size - 1);
        err_out[err_size - 1] = 0;
    }
    return -1;
}

int
auth_make_session_cookie(const char *sub,
                         char *cookie_out, size_t cookie_size)
{
    (void) sub;
    if (cookie_out && cookie_size > 0) cookie_out[0] = 0;
    return -1;
}

int
auth_verify_session_cookie(const char *cookie,
                           char *sub_out, size_t sub_size)
{
    (void) cookie;
    if (sub_out && sub_size > 0) sub_out[0] = 0;
    return -1;
}
