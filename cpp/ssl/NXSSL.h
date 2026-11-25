#pragma once
#include "mbedtls/x509_crt.h"

// load the root certificates from the system, if available. Returns true if successful.
bool hx_nx_ssl_load_root_certs(mbedtls_x509_crt *chain);