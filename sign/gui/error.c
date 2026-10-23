/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <openssl/x509_vfy.h>
#include <p11-kit/pkcs11.h>
#include <sign/error.h>
#include <sign/flow.h>
#include <sign/gui/error.h>
#include <sign/gui/i18n.h>
#include <string.h>
static const char* translated_core(int code) {
	switch (code) {
	case SIGN_ERROR_NO_TOKEN:
		return _(
		    "No signing device was found. Connect the reader and insert your "
		    "card, then try again. Check the selected profile if the device is "
		    "already connected."
		);
	case SIGN_ERROR_MANY_TOKENS:
		return _(
		    "Several signing devices match these settings. Choose a device in "
		    "the signing profile."
		);
	case SIGN_ERROR_NO_CERTIFICATE:
		return _(
		    "No suitable signing certificate was found on the device. Check "
		    "the card and the certificate selected in the profile."
		);
	case SIGN_ERROR_MANY_CERTIFICATES:
		return _(
		    "Several signing certificates match these settings. Choose a "
		    "certificate in the signing profile."
		);
	case SIGN_ERROR_MODULES:
		return _(
		    "The signing device could not be accessed. Check its driver and "
		    "the configured modules."
		);
	case SIGN_ERROR_IMAGE:
		return _(
		    "The signature image could not be used. Choose another image or "
		    "adjust its size and position."
		);
	case SIGN_ERROR_PIN:
		return _(
		    "The PIN could not be obtained. Try again or check the configured "
		    "PIN command."
		);
	case SIGN_ERROR_PIN_INCORRECT:
		return _(
		    "The PIN is incorrect. Check it before trying again to avoid "
		    "blocking the card."
		);
	case SIGN_ERROR_PIN_LOCKED:
		return _(
		    "The card PIN is blocked. Contact your card provider to unblock it."
		);
	case SIGN_ERROR_DEVICE_REMOVED:
		return _(
		    "The signing device was disconnected. Reconnect it and try again."
		);
	case SIGN_ERROR_TRUST:
		return _(
		    "The certificate could not be validated. Check its validity and "
		    "the trusted certificates in Settings."
		);
	case SIGN_ERROR_TIMESTAMP:
		return _(
		    "The timestamp or validation service could not be reached or "
		    "returned an invalid response. Check the connection and signing "
		    "profile."
		);
	case SIGN_ERROR_UNSUPPORTED:
		return _(
		    "The selected format, algorithm or document is not supported. "
		    "Check the signing profile."
		);
	case SIGN_ERROR_INPUT:
		return _(
		    "The document could not be read. Check that it exists and that you "
		    "have permission to open it."
		);
	case SIGN_ERROR_OUTPUT:
		return _(
		    "The signed document could not be saved. Check write permissions "
		    "and whether the output already exists."
		);
	default:
		return _(
		    "The document could not be signed. Check the device and signing "
		    "settings, then try again."
		);
	}
}
const char* gui_trust_error(int status) {
	if (status < 0) {
		return _("Not checked");
	}
	switch (status) {
	case X509_V_OK:
		return _("Trusted chain found");
	case X509_V_ERR_CERT_HAS_EXPIRED:
		return _("The certificate has expired");
	case X509_V_ERR_CERT_NOT_YET_VALID:
		return _("The certificate is not yet valid");
	case X509_V_ERR_CERT_REVOKED:
		return _("The certificate has been revoked");
	case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
	case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
	case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
	case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
		return _("No trusted certificate chain was found");
	default:
		return _("Certificate validation failed");
	}
}
const char* gui_device_error(unsigned long code) {
	switch (code) {
	case CKR_PIN_INCORRECT:
		return translated_core(SIGN_ERROR_PIN_INCORRECT);
	case CKR_PIN_LOCKED:
		return translated_core(SIGN_ERROR_PIN_LOCKED);
	case CKR_DEVICE_REMOVED:
	case CKR_TOKEN_NOT_PRESENT:
		return translated_core(SIGN_ERROR_DEVICE_REMOVED);
	default:
		return _("The device reported an error");
	}
}
static const char* unprefixed(const char* text) {
	if (g_str_has_prefix(text, "documents[")) {
		const char* p = text + 10;
		while (g_ascii_isdigit(*p)) {
			++p;
		}
		if (p > text + 10 && g_str_has_prefix(p, "]: ")) {
			return p + 3;
		}
	}
	return text;
}
char* gui_error_message(const GError* error, gui_error_context context) {
	if (error && error->domain == SIGN_ERROR) {
		return g_strdup(translated_core(error->code));
	}
	if (error && error->domain == SIGN_GUI_ERROR) {
		return g_strdup(unprefixed(error->message));
	}
	if (error &&
	    error->domain == g_quark_from_static_string("sign-flow-error")) {
		if (error->code == SIGN_FLOW_ERROR_BUSY) {
			return g_strdup(
			    _("Another operation is already running. Wait for it to finish "
			      "and try again.")
			);
		}
		return g_strdup(
		    _("These document settings cannot be used together. Check the "
		      "selected profile and signature appearance.")
		);
	}
	if (error && error->domain == G_IO_ERROR) {
		switch (error->code) {
		case G_IO_ERROR_CANCELLED:
			return g_strdup(_("The operation was cancelled."));
		case G_IO_ERROR_NOT_FOUND:
			return g_strdup(
			    _("The file or folder could not be found. Check that it is "
			      "still available.")
			);
		case G_IO_ERROR_PERMISSION_DENIED:
			return g_strdup(
			    _("Access was denied. Check your permissions for this file or "
			      "folder.")
			);
		case G_IO_ERROR_EXISTS:
			return g_strdup(
			    _("The output file already exists. Move or rename it before "
			      "trying again.")
			);
		case G_IO_ERROR_NO_SPACE:
			return g_strdup(
			    _("There is not enough space or memory to complete this "
			      "operation.")
			);
		case G_IO_ERROR_WRONG_ETAG:
			return g_strdup(
			    _("The configuration was changed outside the application. "
			      "Reload it before saving.")
			);
		case G_IO_ERROR_INVALID_DATA:
		case G_IO_ERROR_INVALID_ARGUMENT:
			if (context == GUI_ERROR_SETTINGS) {
				return g_strdup(
				    _("The configuration contains invalid values. Check the "
				      "selected settings and profile labels.")
				);
			}
			break;
		default:
			break;
		}
	}
	switch (context) {
	case GUI_ERROR_SETTINGS:
		return g_strdup(
		    _("The configuration could not be read or saved. Check its values "
		      "and file permissions.")
		);
	case GUI_ERROR_DEVICES:
		return g_strdup(
		    _("Devices could not be loaded. Check the reader connection and "
		      "the modules in Settings.")
		);
	case GUI_ERROR_PDF:
		return g_strdup(
		    _("This PDF could not be opened. It may be damaged or require a "
		      "password.")
		);
	case GUI_ERROR_FILES:
		return g_strdup(
		    _("The selected files could not be opened. Check that they are "
		      "available and that you have permission to read them.")
		);
	case GUI_ERROR_VALIDATION:
		return g_strdup(
		    _("The document could not be validated. Check the file and the "
		      "validation settings.")
		);
	default:
		return g_strdup(translated_core(SIGN_ERROR_CRYPTO));
	}
}
static void close_dialog(GtkButton* button, gpointer window) {
	(void)button;
	gtk_window_destroy(window);
}
void gui_error_dialog(
    GtkWindow*  parent,
    const char* title,
    const char* message
) {
	GtkWidget* window = gtk_window_new();
	gtk_widget_set_name(window, "sign-error-dialog");
	gtk_window_set_title(GTK_WINDOW(window), title);
	gtk_window_set_modal(GTK_WINDOW(window), TRUE);
	gtk_window_set_transient_for(GTK_WINDOW(window), parent);
	gtk_window_set_destroy_with_parent(GTK_WINDOW(window), TRUE);
	if (parent) {
		gtk_window_set_application(
		    GTK_WINDOW(window),
		    gtk_window_get_application(parent)
		);
	}
	gtk_window_set_default_size(GTK_WINDOW(window), 420, 240);
	GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
	gtk_widget_set_margin_start(box, 24);
	gtk_widget_set_margin_end(box, 24);
	gtk_widget_set_margin_top(box, 24);
	gtk_widget_set_margin_bottom(box, 24);
	GtkWidget* label = gtk_label_new(message);
	gtk_label_set_wrap(GTK_LABEL(label), TRUE);
	gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
	gtk_label_set_max_width_chars(GTK_LABEL(label), 48);
	gtk_widget_set_vexpand(label, TRUE);
	gtk_box_append(GTK_BOX(box), label);
	GtkWidget* close = gtk_button_new_with_label(_("Close"));
	gtk_widget_set_name(close, "error-close");
	gtk_widget_add_css_class(close, "suggested-action");
	gtk_widget_set_halign(close, GTK_ALIGN_END);
	g_signal_connect(close, "clicked", G_CALLBACK(close_dialog), window);
	gtk_box_append(GTK_BOX(box), close);
	gtk_window_set_child(GTK_WINDOW(window), box);
	gtk_window_present(GTK_WINDOW(window));
}
