/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <sign/gui/i18n.h>
#include <sign/gui/settings.h>
#include <sign/options.h>
#include <sign/preferences.h>
#include <string.h>

typedef enum { ENTRY, CHOICE, BOOLEAN, MODULES, COMMAND, BUNDLES } input_kind;
typedef struct {
	const char*    key;
	GtkWidget*     widget;
	GtkWidget*     details;
	AdwComboRow*   mode;
	input_kind     kind;
	GtkStringList* choices; /* Canonical model includes Global at index zero. */
} setting_input;
struct _SignSettings {
	GtkBox               parent_instance;
	sign_preferences*    preferences;
	GPtrArray*           inputs;
	AdwComboRow*         scopes;
	GtkWidget *          editor, *remove, *add;
	GtkLabel*            status;
	AdwPreferencesGroup* identity;
	bool                 updating, dirty;
	guint                timer;
};
G_DEFINE_TYPE(SignSettings, sign_settings, GTK_TYPE_BOX)
static sign_preferences_section* selected(SignSettings* self) {
	guint index = adw_combo_row_get_selected(self->scopes);
	return index ? preferences_profile(self->preferences, index - 1)
	             : preferences_global(self->preferences);
}
static void refresh_scopes(SignSettings* self, guint index) {
	GtkStringList* list = gtk_string_list_new(NULL);
	gtk_string_list_append(list, _("Global settings"));
	for (size_t i = 0; i < preferences_count(self->preferences); ++i) {
		sign_preferences_section* profile = preferences_profile(
		    self->preferences,
		    i
		);
		GVariant*   name = preferences_get(profile, "name");
		GVariant*   label = preferences_get(profile, "label");
		const char* display = name ? g_variant_get_string(name, NULL) : NULL;
		if (!display || !display[0]) {
			display = label ? g_variant_get_string(label, NULL)
			                : _("New profile");
		}
		gtk_string_list_append(list, display);
	}
	self->updating = true;
	adw_combo_row_set_model(self->scopes, G_LIST_MODEL(list));
	adw_combo_row_set_selected(self->scopes, index);
	self->updating = false;
	g_object_unref(list);
}
bool sign_settings_flush(SignSettings* self) {
	if (self->timer) {
		g_source_remove(self->timer);
		self->timer = 0;
	}
	if (!self->dirty || !self->preferences) {
		return true;
	}
	GError* error = NULL;
	bool    ok = preferences_save(self->preferences, &error);
	if (ok) {
		self->dirty = false;
		gtk_widget_set_visible(GTK_WIDGET(self->status), FALSE);
		refresh_scopes(self, adw_combo_row_get_selected(self->scopes));
	} else {
		char* message = g_strdup_printf(
		    _("Changes not saved: %s"),
		    error->message
		);
		gtk_label_set_text(self->status, message);
		gtk_widget_set_visible(GTK_WIDGET(self->status), TRUE);
		gtk_widget_add_css_class(GTK_WIDGET(self->status), "error");
		g_free(message);
		g_clear_error(&error);
	}
	return ok;
}
static gboolean save_later(gpointer data) {
	SignSettings* self = data;
	self->timer = 0;
	sign_settings_flush(self);
	return G_SOURCE_REMOVE;
}
static void changed(SignSettings* self) {
	self->dirty = true;
	gtk_widget_set_visible(GTK_WIDGET(self->status), FALSE);
	if (self->timer) {
		g_source_remove(self->timer);
	}
	self->timer = g_timeout_add(500, save_later, self);
}
static char* input_text(setting_input* input) {
	if (input->kind == MODULES) {
		GString* paths = g_string_new(NULL);
		for (GtkWidget* row = gtk_widget_get_first_child(input->widget); row;
		     row = gtk_widget_get_next_sibling(row)) {
			const char* path = gtk_editable_get_text(GTK_EDITABLE(row));
			if (!path[0]) {
				continue;
			}
			if (paths->len) {
				g_string_append_c(paths, '\n');
			}
			g_string_append(paths, path);
		}
		return g_string_free(paths, FALSE);
	}
	return g_strdup(gtk_editable_get_text(GTK_EDITABLE(input->widget)));
}
static void input_changed(GObject* object, gpointer data) {
	SignSettings* self = data;
	if (self->updating || !self->preferences) {
		return;
	}
	setting_input* input = g_object_get_data(object, "setting-input");
	sign_preferences_section* section = selected(self);
	GVariant*                 value = NULL;
	guint offset = adw_combo_row_get_selected(self->scopes) == 0 ? 1 : 0;
	if (input->kind == CHOICE || input->kind == BOOLEAN) {
		guint index = adw_combo_row_get_selected(ADW_COMBO_ROW(input->widget)) +
		              offset;
		if (index == 1) {
			value = g_variant_new_string("");
		} else if (index > 1) {
			if (input->kind == BOOLEAN) {
				value = g_variant_new_boolean(index == 3);
			} else {
				GtkStringObject* item = adw_combo_row_get_selected_item(
				    ADW_COMBO_ROW(input->widget)
				);
				value = g_variant_new_string(
				    gtk_string_object_get_string(item)
				);
			}
		}
	} else {
		guint mode = input->mode
		                 ? adw_combo_row_get_selected(input->mode) + offset
		                 : 2;
		if (input->mode) {
			gtk_widget_set_visible(input->details, mode == 2);
		}
		if (mode == 1) {
			value = (input->kind == MODULES || input->kind == COMMAND ||
			         input->kind == BUNDLES)
			            ? g_variant_new_strv(NULL, 0)
			            : g_variant_new_string("");
		} else if (mode == 2) {
			char* text = input_text(input);
			if (input->kind == MODULES || input->kind == BUNDLES) {
				char** parts = g_strsplit(
				    text,
				    input->kind == MODULES ? "\n" : ",",
				    -1
				);
				GVariantBuilder array;
				g_variant_builder_init(&array, G_VARIANT_TYPE_STRING_ARRAY);
				for (char** item = parts; *item; ++item) {
					if (*g_strstrip(*item)) {
						g_variant_builder_add(&array, "s", *item);
					}
				}
				value = g_variant_builder_end(&array);
				g_strfreev(parts);
			} else if (input->mode || text[0]) {
				value = g_variant_new_string(text);
			}
			g_free(text);
		}
	}
	preferences_set(section, input->key, value);
	changed(self);
}
static void choice_changed(GObject* object, GParamSpec* pspec, gpointer data) {
	(void)pspec;
	input_changed(object, data);
}
static void module_row(
    SignSettings*  self,
    setting_input* input,
    const char*    path
);
static void load_inputs(SignSettings* self) {
	self->updating = true;
	guint index = adw_combo_row_get_selected(self->scopes);
	gtk_widget_set_sensitive(self->remove, index != 0);
	gtk_widget_set_visible(GTK_WIDGET(self->identity), index != 0);
	sign_preferences_section* section = selected(self);
	for (guint i = 0; i < self->inputs->len; ++i) {
		setting_input* input = g_ptr_array_index(self->inputs, i);
		GVariant*      value = preferences_get(section, input->key);
		guint          offset = index == 0 ? 1 : 0;
		if (input->choices) {
			GtkStringList* choices = gtk_string_list_new(NULL);
			for (guint n = offset;
			     n < g_list_model_get_n_items(G_LIST_MODEL(input->choices));
			     ++n) {
				gtk_string_list_append(
				    choices,
				    gtk_string_list_get_string(input->choices, n)
				);
			}
			AdwComboRow* row = input->mode ? input->mode
			                               : ADW_COMBO_ROW(input->widget);
			adw_combo_row_set_model(row, G_LIST_MODEL(choices));
			g_object_unref(choices);
		}
		if (input->kind == CHOICE || input->kind == BOOLEAN) {
			guint choice = 0;
			if (value && g_variant_is_of_type(value, G_VARIANT_TYPE_STRING) &&
			    !g_variant_get_string(value, NULL)[0]) {
				choice = 1;
			} else if (input->kind == BOOLEAN && value) {
				choice = g_variant_get_boolean(value) ? 3 : 2;
			} else if (value) {
				const char* text = g_variant_get_string(value, NULL);
				if (!strcmp(input->key, "algorithm") &&
				    !g_ascii_strcasecmp(text, "rsa")) {
					text = "rsa-pkcs";
				}
				GListModel* model = G_LIST_MODEL(input->choices);
				for (guint n = 2; n < g_list_model_get_n_items(model); ++n) {
					GtkStringObject* item = g_list_model_get_item(model, n);
					if (!g_ascii_strcasecmp(
					        text,
					        gtk_string_object_get_string(item)
					    )) {
						choice = n;
					}
					g_object_unref(item);
				}
			}
			adw_combo_row_set_selected(
			    ADW_COMBO_ROW(input->widget),
			    choice >= offset ? choice - offset : 0
			);
		} else {
			char* text = NULL;
			guint mode = value ? 2 : 0;
			if (value &&
			    g_variant_is_of_type(value, G_VARIANT_TYPE_STRING_ARRAY)) {
				gsize        count;
				const char** items = g_variant_get_strv(value, &count);
				if (!count) {
					mode = 1;
				}
				GString* joined = g_string_new(NULL);
				for (gsize n = 0; n < count; ++n) {
					if (n) {
						g_string_append(
						    joined,
						    input->kind == MODULES
						        ? "\n"
						        : (input->kind == COMMAND ? " " : ", ")
						);
					}
					char* quoted = input->kind == COMMAND
					                   ? g_shell_quote(items[n])
					                   : g_strdup(items[n]);
					g_string_append(joined, quoted);
					g_free(quoted);
				}
				text = g_string_free(joined, FALSE);
				g_free(items);
			} else {
				text = g_strdup(value ? g_variant_get_string(value, NULL) : "");
				if (value && !text[0]) {
					mode = 1;
				}
			}
			if (input->mode) {
				adw_combo_row_set_selected(
				    input->mode,
				    mode >= offset ? mode - offset : 0
				);
				gtk_widget_set_visible(input->details, mode == 2);
			}
			if (input->kind == MODULES) {
				GtkWidget* row;
				while ((row = gtk_widget_get_first_child(input->widget))) {
					gtk_list_box_remove(GTK_LIST_BOX(input->widget), row);
				}
				gtk_widget_set_visible(input->widget, FALSE);
				if (value) {
					gsize        count;
					const char** paths = g_variant_get_strv(value, &count);
					for (gsize n = 0; n < count; ++n) {
						module_row(self, input, paths[n]);
					}
					g_free(paths);
				}
			} else {
				gtk_editable_set_text(GTK_EDITABLE(input->widget), text);
			}
			g_free(text);
		}
	}
	self->updating = false;
}
static void scope_changed(GObject* object, GParamSpec* pspec, gpointer data) {
	(void)object;
	(void)pspec;
	SignSettings* self = data;
	if (!self->updating && self->preferences) {
		load_inputs(self);
	}
}
static void reload(GtkButton* button, gpointer data) {
	(void)button;
	SignSettings* self = data;
	if (self->timer) {
		g_source_remove(self->timer);
		self->timer = 0;
	}
	self->dirty = false;
	sign_preferences* loaded;
	GError*           error = NULL;
	loaded = preferences_load(&error);
	if (!loaded) {
		gtk_label_set_text(self->status, error->message);
		gtk_widget_set_visible(GTK_WIDGET(self->status), TRUE);
		gtk_widget_add_css_class(GTK_WIDGET(self->status), "error");
		gtk_widget_set_sensitive(self->editor, FALSE);
		gtk_widget_set_sensitive(self->add, FALSE);
		gtk_widget_set_sensitive(self->remove, FALSE);
		gtk_widget_set_sensitive(GTK_WIDGET(self->scopes), FALSE);
		g_clear_error(&error);
		return;
	}
	preferences_free(self->preferences);
	self->preferences = loaded;
	self->dirty = false;
	gtk_widget_set_visible(GTK_WIDGET(self->status), FALSE);
	gtk_widget_set_sensitive(self->editor, TRUE);
	gtk_widget_set_sensitive(self->add, TRUE);
	gtk_widget_set_sensitive(GTK_WIDGET(self->scopes), TRUE);
	refresh_scopes(self, 0);
	load_inputs(self);
}
static void add_profile(GtkButton* button, gpointer data) {
	(void)button;
	SignSettings* self = data;
	guint         serial = 1;
	char*         label = NULL;
	bool          exists;
	do {
		g_free(label);
		label = g_strdup_printf("profile-%u", serial++);
		exists = false;
		for (size_t i = 0; i < preferences_count(self->preferences); ++i) {
			GVariant* value = preferences_get(
			    preferences_profile(self->preferences, i),
			    "label"
			);
			exists |= value &&
			          !strcmp(g_variant_get_string(value, NULL), label);
		}
	} while (exists);
	sign_preferences_section* profile = preferences_add(self->preferences);
	preferences_set(profile, "label", g_variant_new_string(label));
	g_free(label);
	refresh_scopes(self, preferences_count(self->preferences));
	load_inputs(self);
	changed(self);
}
static void remove_profile(GtkButton* button, gpointer data) {
	(void)button;
	SignSettings* self = data;
	guint         index = adw_combo_row_get_selected(self->scopes);
	if (!index) {
		return;
	}
	preferences_remove(self->preferences, index - 1);
	refresh_scopes(self, 0);
	load_inputs(self);
	changed(self);
}
static AdwPreferencesGroup* group(
    SignSettings* self,
    const char*   title,
    const char*   description
) {
	AdwPreferencesGroup* group = ADW_PREFERENCES_GROUP(
	    adw_preferences_group_new()
	);
	if (title) {
		adw_preferences_group_set_title(group, title);
	}
	if (description) {
		adw_preferences_group_set_description(group, description);
	}
	adw_preferences_page_add(ADW_PREFERENCES_PAGE(self->editor), group);
	return group;
}
static GtkWidget* combo(const char* title, const char* const* choices) {
	GtkWidget* row = adw_combo_row_new();
	adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
	GtkStringList* list = gtk_string_list_new(choices);
	adw_combo_row_set_model(ADW_COMBO_ROW(row), G_LIST_MODEL(list));
	g_object_unref(list);
	return row;
}
static void connect_input(
    SignSettings*  self,
    setting_input* input,
    GObject*       object,
    const char*    signal
) {
	g_object_set_data(object, "setting-input", input);
	g_signal_connect(object, signal, G_CALLBACK(input_changed), self);
}
static void module_remove(GtkButton* button, gpointer data) {
	SignSettings*  self = data;
	setting_input* input = g_object_get_data(G_OBJECT(button), "setting-input");
	GtkWidget*     row = g_object_get_data(G_OBJECT(button), "module-row");
	gtk_list_box_remove(GTK_LIST_BOX(input->widget), row);
	gtk_widget_set_visible(
	    input->widget,
	    gtk_widget_get_first_child(input->widget) != NULL
	);
	input_changed(G_OBJECT(input->widget), self);
}
static void module_row(
    SignSettings*  self,
    setting_input* input,
    const char*    path
) {
	GtkWidget* row = adw_entry_row_new();
	adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), _("Module path"));
	gtk_editable_set_text(GTK_EDITABLE(row), path);
	GtkWidget* remove = gtk_button_new_from_icon_name("list-remove-symbolic");
	gtk_widget_set_valign(remove, GTK_ALIGN_CENTER);
	gtk_widget_add_css_class(remove, "flat");
	gtk_widget_set_tooltip_text(remove, _("Remove module"));
	gtk_widget_set_name(remove, "remove-module");
	adw_entry_row_add_suffix(ADW_ENTRY_ROW(row), remove);
	g_object_set_data(G_OBJECT(remove), "setting-input", input);
	g_object_set_data(G_OBJECT(remove), "module-row", row);
	g_signal_connect(remove, "clicked", G_CALLBACK(module_remove), self);
	connect_input(self, input, G_OBJECT(row), "changed");
	gtk_list_box_append(GTK_LIST_BOX(input->widget), row);
	gtk_widget_set_visible(input->widget, TRUE);
}
static void module_add(GtkButton* button, gpointer data) {
	SignSettings*  self = data;
	setting_input* input = g_object_get_data(G_OBJECT(button), "setting-input");
	module_row(self, input, "");
	gtk_widget_grab_focus(gtk_widget_get_last_child(input->widget));
}
static void entry(
    SignSettings*        self,
    AdwPreferencesGroup* group,
    const char*          key,
    const char*          title,
    input_kind           kind
) {
	setting_input* input = g_new0(setting_input, 1);
	input->key = key;
	input->kind = kind;
	if (strcmp(key, "name") && strcmp(key, "label")) {
		const char* modes[] = {_("Global"), _("Automatic"), _("Custom"), NULL};
		input->mode = ADW_COMBO_ROW(combo(title, modes));
		input->choices = g_object_ref(
		    GTK_STRING_LIST(adw_combo_row_get_model(input->mode))
		);
		adw_preferences_group_add(group, GTK_WIDGET(input->mode));
		g_object_set_data(G_OBJECT(input->mode), "setting-input", input);
		g_signal_connect(
		    input->mode,
		    "notify::selected",
		    G_CALLBACK(choice_changed),
		    self
		);
		char* name = g_strconcat(key, "-mode", NULL);
		gtk_widget_set_name(GTK_WIDGET(input->mode), name);
		g_free(name);
	}
	if (kind == MODULES) {
		input->details = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
		gtk_widget_set_margin_top(input->details, 12);
		input->widget = gtk_list_box_new();
		gtk_widget_add_css_class(input->widget, "boxed-list");
		gtk_widget_set_visible(input->widget, FALSE);
		gtk_list_box_set_selection_mode(
		    GTK_LIST_BOX(input->widget),
		    GTK_SELECTION_NONE
		);
		g_object_set_data(G_OBJECT(input->widget), "setting-input", input);
		gtk_box_append(GTK_BOX(input->details), input->widget);
		GtkWidget* add = gtk_button_new_with_label(_("Add module"));
		gtk_widget_set_name(add, "add-module");
		gtk_widget_set_halign(add, GTK_ALIGN_END);
		g_object_set_data(G_OBJECT(add), "setting-input", input);
		g_signal_connect(add, "clicked", G_CALLBACK(module_add), self);
		gtk_box_append(GTK_BOX(input->details), add);
		adw_preferences_group_add(group, input->details);
	} else {
		input->widget = adw_entry_row_new();
		input->details = input->widget;
		adw_preferences_row_set_title(
		    ADW_PREFERENCES_ROW(input->widget),
		    input->mode ? _("Value") : title
		);
		adw_preferences_group_add(group, input->widget);
		connect_input(self, input, G_OBJECT(input->widget), "changed");
	}
	gtk_widget_set_name(input->widget, key);
	g_ptr_array_add(self->inputs, input);
}
static void choice(
    SignSettings*        self,
    AdwPreferencesGroup* group,
    const char*          key,
    const char*          title,
    sign_option          option,
    bool                 boolean
) {
	GtkStringList* list = gtk_string_list_new(NULL);
	gtk_string_list_append(list, _("Global"));
	gtk_string_list_append(list, _("Automatic"));
	if (boolean) {
		gtk_string_list_append(list, _("No"));
		gtk_string_list_append(list, _("Yes"));
	} else {
		const char* item;
		for (guint i = 0; (item = sign_option_choice(option, i)); ++i) {
			if (option == SIGN_OPTION_FORMAT && i == LIBSSIG_FORMAT_CMS) {
				continue;
			}
			gtk_string_list_append(list, item);
		}
	}
	setting_input* input = g_new0(setting_input, 1);
	input->key = key;
	input->kind = boolean ? BOOLEAN : CHOICE;
	input->widget = adw_combo_row_new();
	adw_preferences_row_set_title(ADW_PREFERENCES_ROW(input->widget), title);
	adw_combo_row_set_model(ADW_COMBO_ROW(input->widget), G_LIST_MODEL(list));
	input->choices = g_object_ref(list);
	g_object_unref(list);
	gtk_widget_set_name(input->widget, key);
	adw_preferences_group_add(group, input->widget);
	g_object_set_data(G_OBJECT(input->widget), "setting-input", input);
	g_signal_connect(
	    input->widget,
	    "notify::selected",
	    G_CALLBACK(choice_changed),
	    self
	);
	g_ptr_array_add(self->inputs, input);
}
static void input_free(gpointer data) {
	setting_input* input = data;
	g_clear_object(&input->choices);
	g_free(input);
}
static void sign_settings_dispose(GObject* object) {
	SignSettings* self = SIGN_SETTINGS(object);
	sign_settings_flush(self);
	preferences_free(self->preferences);
	self->preferences = NULL;
	G_OBJECT_CLASS(sign_settings_parent_class)->dispose(object);
}
static void sign_settings_finalize(GObject* object) {
	SignSettings* self = SIGN_SETTINGS(object);
	g_ptr_array_unref(self->inputs);
	G_OBJECT_CLASS(sign_settings_parent_class)->finalize(object);
}
static void sign_settings_class_init(SignSettingsClass* klass) {
	G_OBJECT_CLASS(klass)->dispose = sign_settings_dispose;
	G_OBJECT_CLASS(klass)->finalize = sign_settings_finalize;
}
static void sign_settings_init(SignSettings* self) {
	gtk_orientable_set_orientation(
	    GTK_ORIENTABLE(self),
	    GTK_ORIENTATION_VERTICAL
	);
	self->inputs = g_ptr_array_new_with_free_func(input_free);
	self->editor = adw_preferences_page_new();
	gtk_widget_set_vexpand(self->editor, TRUE);
	gtk_box_append(GTK_BOX(self), self->editor);
	AdwPreferencesGroup* selection = group(self, NULL, NULL);
	self->scopes = ADW_COMBO_ROW(combo(_("Configuration"), NULL));
	gtk_widget_set_name(GTK_WIDGET(self->scopes), "settings-scope");
	adw_preferences_group_add(selection, GTK_WIDGET(self->scopes));
	GtkWidget* controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign(controls, GTK_ALIGN_END);
	gtk_widget_set_margin_top(controls, 6);
	self->add = gtk_button_new_from_icon_name("list-add-symbolic");
	gtk_widget_set_tooltip_text(self->add, _("Create profile"));
	gtk_widget_set_name(self->add, "add-profile");
	self->remove = gtk_button_new_from_icon_name("list-remove-symbolic");
	gtk_widget_set_tooltip_text(self->remove, _("Delete profile"));
	gtk_widget_set_name(self->remove, "remove-profile");
	GtkWidget* reload_button = gtk_button_new_from_icon_name(
	    "view-refresh-symbolic"
	);
	gtk_widget_set_tooltip_text(reload_button, _("Reload configuration"));
	gtk_widget_set_name(reload_button, "reload-settings");
	gtk_box_append(GTK_BOX(controls), self->remove);
	gtk_box_append(GTK_BOX(controls), self->add);
	gtk_box_append(GTK_BOX(controls), reload_button);
	adw_preferences_group_add(selection, controls);
	self->status = GTK_LABEL(gtk_label_new(NULL));
	gtk_label_set_wrap(self->status, TRUE);
	gtk_label_set_wrap_mode(self->status, PANGO_WRAP_WORD_CHAR);
	gtk_widget_set_name(GTK_WIDGET(self->status), "settings-status");
	gtk_widget_add_css_class(GTK_WIDGET(self->status), "error");
	adw_preferences_group_add(selection, GTK_WIDGET(self->status));
	self->identity = group(
	    self,
	    _("Profile"),
	    _("The label selects this profile with --profile.")
	);
	entry(self, self->identity, "name", _("Name"), ENTRY);
	entry(self, self->identity, "label", _("Label"), ENTRY);
	AdwPreferencesGroup* signing = group(
	    self,
	    _("Signing defaults"),
	    _("Global inherits the global value. Automatic uses the engine "
	      "default.")
	);
	entry(self, signing, "token", _("Token URI"), ENTRY);
	entry(self, signing, "cert_id", _("Certificate ID"), ENTRY);
	choice(
	    self,
	    signing,
	    "algorithm",
	    _("Algorithm"),
	    SIGN_OPTION_ALGORITHM,
	    false
	);
	choice(self, signing, "hash", _("Hash"), SIGN_OPTION_HASH, false);
	choice(
	    self,
	    signing,
	    "level",
	    _("Signature level"),
	    SIGN_OPTION_LEVEL,
	    false
	);
	choice(self, signing, "format", _("Format"), SIGN_OPTION_FORMAT, false);
	choice(
	    self,
	    signing,
	    "detached",
	    _("Detached signature"),
	    SIGN_OPTION_DETACHED,
	    true
	);
	entry(self, signing, "signature_image", _("Signature image"), ENTRY);
	AdwPreferencesGroup* tsa = group(self, _("Timestamp authority"), NULL);
	entry(self, tsa, "tsa.url", _("URL"), ENTRY);
	entry(self, tsa, "tsa.ca", _("TSA trust certificates"), ENTRY);
	entry(
	    self,
	    tsa,
	    "tsa.intermediates",
	    _("TSA intermediate certificates"),
	    ENTRY
	);
	AdwPreferencesGroup* trust = group(
	    self,
	    _("Document trust"),
	    _("Relative paths resolve against the configuration directory.")
	);
	entry(self, trust, "ca", _("Trust certificates"), ENTRY);
	entry(self, trust, "intermediates", _("Intermediate certificates"), ENTRY);
	choice(
	    self,
	    trust,
	    "no_bundled_certs",
	    _("Disable bundled certificates"),
	    SIGN_OPTION_NO_BUNDLED_CERTS,
	    true
	);
	entry(
	    self,
	    trust,
	    "cert_bundles",
	    _("Certificate bundles (comma separated)"),
	    BUNDLES
	);
	AdwPreferencesGroup* modules = group(
	    self,
	    _("PKCS#11 modules"),
	    _("Automatic uses modules registered with p11-kit.")
	);
	entry(self, modules, "modules", _("Module paths"), MODULES);
	AdwPreferencesGroup* pin = group(
	    self,
	    _("PIN command"),
	    _("Command and arguments, with shell-style quoting. The PIN is "
	      "requested only when signing.")
	);
	entry(self, pin, "pincmd", _("Command"), COMMAND);
	g_signal_connect(
	    self->scopes,
	    "notify::selected",
	    G_CALLBACK(scope_changed),
	    self
	);
	g_signal_connect(self->add, "clicked", G_CALLBACK(add_profile), self);
	g_signal_connect(self->remove, "clicked", G_CALLBACK(remove_profile), self);
	g_signal_connect(reload_button, "clicked", G_CALLBACK(reload), self);
	reload(NULL, self);
}
GtkWidget* sign_settings_new(void) {
	return g_object_new(SIGN_TYPE_SETTINGS, NULL);
}
