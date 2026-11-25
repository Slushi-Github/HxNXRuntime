#include "NXSSL.h"

#if defined(HX_NX)

// SSL.cpp has no internal caller of _hx_ssl_cert_load_defaults; only the
// generated Haxe code calls it through StdLibs.h. Renaming it while including
// the whole file keeps every other ssl symbol and lets us provide the Switch
// implementation of just that function.
#define _hx_ssl_cert_load_defaults _hx_ssl_cert_load_defaults_orig
#include "SSL.cpp"
#include "NXExtraCACerts.h"
#undef _hx_ssl_cert_load_defaults
#endif

Dynamic _hx_ssl_cert_load_defaults() {
#if defined(HX_NX)
	sslcert *chain = new sslcert();
	chain->create( NULL );
	bool loaded = hx_nx_ssl_load_root_certs( chain->c );
	// Roots published by CAs that HorizonOS store may be missing. They are
	// added on top of the system store, never replacing it, so verification
	// stays enabled for every certificate. Some servers (example.com among
	// them) send a cross-signed root whose chain only verifies against the
	// self-signed root, which is exactly what browsers' stores provide.
	int extra = mbedtls_x509_crt_parse( chain->c,
		(const unsigned char *)nx_extra_ca_pem, sizeof(nx_extra_ca_pem) );
	if( loaded || extra == 0 )
		return chain;
	chain->destroy();
	return null();
#else
	return null();
#endif
}
