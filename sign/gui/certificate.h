/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SIGN_GUI_CERTIFICATE_H
#define SIGN_GUI_CERTIFICATE_H
#include <openssl/x509.h>
char* sign_certificate_attribute(X509_NAME* name, int nid);
char* sign_certificate_display_name(X509_NAME* name);
char* sign_certificate_name(X509_NAME* name);
char* sign_certificate_time(const ASN1_TIME* value);
#endif
