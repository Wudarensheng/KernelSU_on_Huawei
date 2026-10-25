cd kernel-source
curl -LSs "https://raw.githubusercontent.com/Baka-SU/BakaSU/main/kernel/setup.sh" | bash

# Fix selinux_hide.c
sed -i \
  -e 's/\bkmemdup_nul\b/ksu_kmemdup_nul/g' \
  -e '/#include "compat\/kernel_compat.h"/a\
\
/* Local kmemdup_nul fallback for pre-3.18.9 kernels (upstream backport 2018) */\
static inline char *ksu_kmemdup_nul(const char *s, size_t len, gfp_t gfp)\
{\
\tchar *buf;\
\
\tif (!s)\
\t\treturn NULL;\
\
\tbuf = kmalloc(len + 1, gfp);\
\tif (buf) {\
\t\tmemcpy(buf, s, len);\
\t\tbuf[len] = 0;\
\t}\
\treturn buf;\
}' \
  drivers/kernelsu/feature/selinux_hide.c