#include "NXSSL.h"

#if defined(HX_NX)
#include <switch.h>
#include <stdlib.h>
#include <time.h>

// Custom way of loading root certificates on Switch 
// (Thanks CRobes on Discord for telling me that I had to modify this function)
bool hx_nx_ssl_load_root_certs(mbedtls_x509_crt *chain)
{
	bool loaded = false;

	if (R_FAILED(sslInitialize(3)))
		return false;

	u32 id = SslCaCertificateId_All;
	u32 buf_size = 0;

	if (R_SUCCEEDED(sslGetCertificateBufSize(&id, 1, &buf_size)))
	{
		void *buf = malloc(buf_size);
		if (buf)
		{
			u32 count = 0;
			if (R_SUCCEEDED(sslGetCertificates(buf, buf_size, &id, 1, &count)))
			{
				SslBuiltInCertificateInfo *infos = (SslBuiltInCertificateInfo *)buf;
				for (u32 i = 0; i < count; i++)
				{
					if (infos[i].status == SslTrustedCertStatus_EnabledTrusted)
					{
						if (mbedtls_x509_crt_parse_der(chain,
								(unsigned char *)infos[i].cert_data,
								(size_t)infos[i].cert_size) == 0)
							loaded = true;
					}
				}
			}
			free(buf);
		}
	}

	sslExit();
	return loaded;
}

#if defined(MBEDTLS_ENTROPY_HARDWARE_ALT)
// HorizonOS has no /dev/urandom, so mbedtls must get entropy from the platform.
// DevkitA64/libnx exposes the kernel RNG through randomGet().
extern "C" int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen)
{
	(void)data;
	randomGet(output, len);
	*olen = len;
	return 0;
}
#endif

#if defined(MBEDTLS_PLATFORM_TIME_ALT)
#include <mbedtls/platform_time.h>

// devkitA64's newlib `time()` is not wired to the HorizonOS RTC in devkitA64, so mbedtls
// would see certificates as "not yet valid". Use the time service instead.
static mbedtls_time_t hx_nx_mbedtls_time(mbedtls_time_t *timer)
{
	u64 ts = 0;
	if (R_FAILED(timeGetCurrentTime(TimeType_UserSystemClock, &ts)))
		ts = (u64)time(NULL);
	if (timer)
		*timer = (mbedtls_time_t)ts;
	return (mbedtls_time_t)ts;
}

static struct NxMbedtlsTimeInit
{
	NxMbedtlsTimeInit()
	{
		mbedtls_platform_set_time(hx_nx_mbedtls_time);
	}
} s_nx_mbedtls_time_init;
#endif

#if defined(MBEDTLS_PLATFORM_MS_TIME_ALT)
#include <mbedtls/platform_time.h>
// mbedtls' built-in millisecond clock needs POSIX clock_gettime, which
// devkitA64's newlib does not expose. The ARM system tick is monotonic.
extern "C" mbedtls_ms_time_t mbedtls_ms_time(void)
{
	u64 freq = armGetSystemTickFreq();
	return (mbedtls_ms_time_t)(armGetSystemTick() / (freq / 1000));
}
#endif
#endif