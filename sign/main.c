#include <argp.h>
#include <stdbool.h>

#ifdef SIGN_WITH_GUI
#include <gtk/gtk.h>
#else
#include <stdio.h>
#include <stdlib.h>
#endif

const char* argp_program_version = "sign " SIGN_VERSION;

static const struct argp_option options[] = {
    {"gui", 'g', 0, 0, "Open the graphical interface", 0},
    {"help", 'h', 0, 0, "Show this help message", 0},
    {0}
};

static error_t parse_option(int key, char* arg, struct argp_state* state) {
	(void)arg;
	bool* gui = state->input;

	switch (key) {
	case 'g':
		*gui = true;
		return 0;
	case 'h':
		argp_state_help(state, stdout, ARGP_HELP_STD_HELP);
		return 0;
	default:
		return ARGP_ERR_UNKNOWN;
	}
}

static const struct argp parser = {
    .options = options,
    .parser = parse_option,
    .doc = "crsign: digital signing application."
};

#ifdef SIGN_WITH_GUI
static void activate(GtkApplication* app, gpointer user_data) {
	(void)user_data;
	GtkWidget* window = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(window), "sign");
	gtk_window_set_default_size(GTK_WINDOW(window), 320, 160);
	gtk_window_set_child(GTK_WINDOW(window), gtk_label_new("Hello, world!"));
	gtk_window_present(GTK_WINDOW(window));
}
#endif

int main(int argc, char** argv) {
	bool gui = false;
	argp_parse(&parser, argc, argv, 0, 0, &gui);

	if (!gui) {
		argp_help(&parser, stdout, ARGP_HELP_STD_HELP, argv[0]);
		return 0;
	}

#ifdef SIGN_WITH_GUI
	GtkApplication* app = gtk_application_new(
	    "org.crsign.Sign",
	    G_APPLICATION_DEFAULT_FLAGS
	);
	g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
	int status = g_application_run(G_APPLICATION(app), 1, argv);
	g_object_unref(app);
	return status;
#else
	fprintf(stderr, "sign: GUI unavailable in this headless build\n");
	return EXIT_FAILURE;
#endif
}
