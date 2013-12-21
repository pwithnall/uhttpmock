/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 * uhttpmock
 * Copyright (C) Philip Withnall 2013 <philip@tecnocode.co.uk>
 *
 * uhttpmock is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * uhttpmock is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with uhttpmock.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <glib.h>
#include <string.h>
#include <unistd.h>

static UhmServer *mock_server = NULL;

static void
test_authentication (void)
{
	gboolean retval;
	GDataClientLoginAuthorizer *authorizer;
	GError *error = NULL;

	uhm_server_start_trace (mock_server, "authentication", NULL);
	gdata_set_https_port (uhm_server_get_port (mock_server));

	/* Create an authorizer */
	authorizer = gdata_client_login_authorizer_new (CLIENT_ID, GDATA_TYPE_YOUTUBE_SERVICE);

	g_assert_cmpstr (gdata_client_login_authorizer_get_client_id (authorizer), ==, CLIENT_ID);

	/* Log in */
	retval = gdata_client_login_authorizer_authenticate (authorizer, USERNAME, PASSWORD, NULL, &error);
	g_assert_no_error (error);
	g_assert (retval == TRUE);
	g_clear_error (&error);

	/* Check all is as it should be */
	g_assert_cmpstr (gdata_client_login_authorizer_get_username (authorizer), ==, USERNAME);
	g_assert_cmpstr (gdata_client_login_authorizer_get_password (authorizer), ==, PASSWORD);

	g_assert (gdata_authorizer_is_authorized_for_domain (GDATA_AUTHORIZER (authorizer),
	                                                     gdata_youtube_service_get_primary_authorization_domain ()) == TRUE);

	g_object_unref (authorizer);

	uhm_server_end_trace (mock_server);
}

/* HTTP message responses and the expected associated GData error domain/code. */
static const GDataTestRequestErrorData authentication_errors[] = {
	/* Generic network errors. */
	{ SOUP_STATUS_BAD_REQUEST, "Bad Request", "Invalid parameter ‘foobar’.",
	  gdata_service_error_quark, GDATA_SERVICE_ERROR_PROTOCOL_ERROR },
	{ SOUP_STATUS_NOT_FOUND, "Not Found", "Login page wasn't found for no good reason at all.",
	  gdata_service_error_quark, GDATA_SERVICE_ERROR_NOT_FOUND },
	{ SOUP_STATUS_PRECONDITION_FAILED, "Precondition Failed", "Not allowed to log in at this time, possibly.",
	  gdata_service_error_quark, GDATA_SERVICE_ERROR_CONFLICT },
	{ SOUP_STATUS_INTERNAL_SERVER_ERROR, "Internal Server Error", "Whoops.",
	  gdata_service_error_quark, GDATA_SERVICE_ERROR_PROTOCOL_ERROR },
	/* Specific authentication errors. */
	{ SOUP_STATUS_FORBIDDEN, "Access Forbidden", "Error=BadAuthentication\n",
	  gdata_client_login_authorizer_error_quark, GDATA_CLIENT_LOGIN_AUTHORIZER_ERROR_BAD_AUTHENTICATION },
	{ SOUP_STATUS_FORBIDDEN, "Access Forbidden", "Error=BadAuthentication\nInfo=InvalidSecondFactor\n",
	  gdata_client_login_authorizer_error_quark, GDATA_CLIENT_LOGIN_AUTHORIZER_ERROR_INVALID_SECOND_FACTOR },
	{ SOUP_STATUS_FORBIDDEN, "Access Forbidden", "Error=NotVerified\nUrl=http://example.com/\n",
	  gdata_client_login_authorizer_error_quark, GDATA_CLIENT_LOGIN_AUTHORIZER_ERROR_NOT_VERIFIED },
	{ SOUP_STATUS_FORBIDDEN, "Access Forbidden", "Error=TermsNotAgreed\nUrl=http://example.com/\n",
	  gdata_client_login_authorizer_error_quark, GDATA_CLIENT_LOGIN_AUTHORIZER_ERROR_TERMS_NOT_AGREED },
	{ SOUP_STATUS_FORBIDDEN, "Access Forbidden", "Error=Unknown\nUrl=http://example.com/\n",
	  gdata_service_error_quark, GDATA_SERVICE_ERROR_AUTHENTICATION_REQUIRED },
	{ SOUP_STATUS_FORBIDDEN, "Access Forbidden", "Error=AccountDeleted\nUrl=http://example.com/\n",
	  gdata_client_login_authorizer_error_quark, GDATA_CLIENT_LOGIN_AUTHORIZER_ERROR_ACCOUNT_DELETED },
	{ SOUP_STATUS_FORBIDDEN, "Access Forbidden", "Error=AccountDisabled\nUrl=http://example.com/\n",
	  gdata_client_login_authorizer_error_quark, GDATA_CLIENT_LOGIN_AUTHORIZER_ERROR_ACCOUNT_DISABLED },
	{ SOUP_STATUS_FORBIDDEN, "Access Forbidden", "Error=AccountMigrated\nUrl=http://example.com/\n",
	  gdata_client_login_authorizer_error_quark, GDATA_CLIENT_LOGIN_AUTHORIZER_ERROR_ACCOUNT_MIGRATED },
	{ SOUP_STATUS_FORBIDDEN, "Access Forbidden", "Error=ServiceDisabled\nUrl=http://example.com/\n",
	  gdata_client_login_authorizer_error_quark, GDATA_CLIENT_LOGIN_AUTHORIZER_ERROR_SERVICE_DISABLED },
	{ SOUP_STATUS_FORBIDDEN, "Access Forbidden", "Error=ServiceUnavailable\nUrl=http://example.com/\n",
	  gdata_service_error_quark, GDATA_SERVICE_ERROR_UNAVAILABLE },
	/* Malformed authentication errors to test parser error handling. */
	{ SOUP_STATUS_INTERNAL_SERVER_ERROR, "Access Forbidden", "Error=BadAuthentication", /* missing Error delimiter */
	  gdata_service_error_quark, GDATA_SERVICE_ERROR_PROTOCOL_ERROR },
	{ SOUP_STATUS_INTERNAL_SERVER_ERROR, "Access Forbidden", "Error=AccountDeleted\n", /* missing Url */
	  gdata_service_error_quark, GDATA_SERVICE_ERROR_PROTOCOL_ERROR },
	{ SOUP_STATUS_INTERNAL_SERVER_ERROR, "Access Forbidden", "Error=AccountDeleted\nUrl=http://example.com/", /* missing Url delimiter */
	  gdata_service_error_quark, GDATA_SERVICE_ERROR_PROTOCOL_ERROR },
	{ SOUP_STATUS_INTERNAL_SERVER_ERROR, "Access Forbidden", "", /* missing Error */
	  gdata_service_error_quark, GDATA_SERVICE_ERROR_PROTOCOL_ERROR },
	{ SOUP_STATUS_INTERNAL_SERVER_ERROR, "Access Forbidden", "Error=", /* missing Error */
	  gdata_service_error_quark, GDATA_SERVICE_ERROR_PROTOCOL_ERROR },
	{ SOUP_STATUS_INTERNAL_SERVER_ERROR, "Access Forbidden", "Error=Foobar\nUrl=http://example.com/\n", /* unknown Error */
	  gdata_service_error_quark, GDATA_SERVICE_ERROR_PROTOCOL_ERROR },
};

/**
 * mock_server_handle_message_error:
 * @server: a #UhmServer
 * @message: the message whose response should be filled
 * @client: the currently connected client
 * @user_data: user data provided when connecting the signal
 *
 * Handler for #UhmServer::handle-message which sets the HTTP response for @message to the HTTP error status
 * specified in a #GDataTestRequestErrorData structure passed to @user_data.
 *
 * Since: 0.13.4
 */
static gboolean
handle_message_error_cb (UhmServer *server, SoupMessage *message, SoupClientContext *client, gpointer user_data)
{
	const GDataTestRequestErrorData *data = user_data;

	soup_message_set_status_full (message, data->status_code, data->reason_phrase);
	soup_message_body_append (message->response_body, SOUP_MEMORY_STATIC, data->message_body, strlen (data->message_body));

	return TRUE;
}

/**
 * mock_server_handle_message_timeout:
 * @server: a #UhmServer
 * @message: the message whose response should be filled
 * @client: the currently connected client
 * @user_data: user data provided when connecting the signal
 *
 * Handler for #UhmServer::handle-message which waits for 2 seconds before returning a %SOUP_STATUS_REQUEST_TIMEOUT status
 * and appropriate error message body. If used in conjunction with a 1 second timeout in the client code under test, this can
 * simulate network error conditions and timeouts, in order to test the error handling code for such conditions.
 *
 * Since: 0.13.4
 */
static gboolean
handle_message_timeout_cb (UhmServer *server, SoupMessage *message, SoupClientContext *client, gpointer user_data)
{
	/* Sleep for longer than the timeout set on the client. */
	g_usleep (2 * G_USEC_PER_SEC);

	soup_message_set_status_full (message, SOUP_STATUS_REQUEST_TIMEOUT, "Request Timeout");
	soup_message_body_append (message->response_body, SOUP_MEMORY_STATIC, "Request timed out.", strlen ("Request timed out."));

	return TRUE;
}

static void
test_authentication_error (void)
{
	gboolean retval;
	GDataClientLoginAuthorizer *authorizer;
	GError *error = NULL;
	gulong handler_id;
	guint i;

	if (uhm_server_get_enable_logging (mock_server) == TRUE) {
		g_test_message ("Ignoring test due to logging being enabled.");
		return;
	} else if (uhm_server_get_enable_online (mock_server) == TRUE) {
		g_test_message ("Ignoring test due to running online and test not being reproducible.");
		return;
	}

	for (i = 0; i < G_N_ELEMENTS (authentication_errors); i++) {
		const GDataTestRequestErrorData *data = &authentication_errors[i];

		handler_id = g_signal_connect (mock_server, "handle-message", (GCallback) handle_message_error_cb, (gpointer) data);
		uhm_server_run (mock_server);
		gdata_set_https_port (uhm_server_get_port (mock_server));

		/* Create an authorizer */
		authorizer = gdata_client_login_authorizer_new (CLIENT_ID, GDATA_TYPE_YOUTUBE_SERVICE);

		g_assert_cmpstr (gdata_client_login_authorizer_get_client_id (authorizer), ==, CLIENT_ID);

		/* Log in */
		retval = gdata_client_login_authorizer_authenticate (authorizer, USERNAME, PASSWORD, NULL, &error);
		g_assert_error (error, data->error_domain_func (), data->error_code);
		g_assert (retval == FALSE);
		g_clear_error (&error);

		/* Check nothing's changed in the authoriser. */
		g_assert_cmpstr (gdata_client_login_authorizer_get_username (authorizer), ==, NULL);
		g_assert_cmpstr (gdata_client_login_authorizer_get_password (authorizer), ==, NULL);

		g_assert (gdata_authorizer_is_authorized_for_domain (GDATA_AUTHORIZER (authorizer),
		                                                     gdata_youtube_service_get_primary_authorization_domain ()) == FALSE);

		g_object_unref (authorizer);

		uhm_server_stop (mock_server);
		g_signal_handler_disconnect (mock_server, handler_id);
	}
}

static void
test_authentication_timeout (void)
{
	gboolean retval;
	GDataClientLoginAuthorizer *authorizer;
	GError *error = NULL;
	gulong handler_id;

	if (uhm_server_get_enable_logging (mock_server) == TRUE) {
		g_test_message ("Ignoring test due to logging being enabled.");
		return;
	} else if (uhm_server_get_enable_online (mock_server) == TRUE) {
		g_test_message ("Ignoring test due to running online and test not being reproducible.");
		return;
	}

	handler_id = g_signal_connect (mock_server, "handle-message", (GCallback) handle_message_timeout_cb, NULL);
	uhm_server_run (mock_server);
	gdata_set_https_port (uhm_server_get_port (mock_server));

	/* Create an authorizer and set its timeout as low as possible (1 second). */
	authorizer = gdata_client_login_authorizer_new (CLIENT_ID, GDATA_TYPE_YOUTUBE_SERVICE);
	gdata_client_login_authorizer_set_timeout (authorizer, 1);

	g_assert_cmpstr (gdata_client_login_authorizer_get_client_id (authorizer), ==, CLIENT_ID);

	/* Log in */
	retval = gdata_client_login_authorizer_authenticate (authorizer, USERNAME, PASSWORD, NULL, &error);
	g_assert_error (error, GDATA_SERVICE_ERROR, GDATA_SERVICE_ERROR_NETWORK_ERROR);
	g_assert (retval == FALSE);
	g_clear_error (&error);

	/* Check nothing's changed in the authoriser. */
	g_assert_cmpstr (gdata_client_login_authorizer_get_username (authorizer), ==, NULL);
	g_assert_cmpstr (gdata_client_login_authorizer_get_password (authorizer), ==, NULL);

	g_assert (gdata_authorizer_is_authorized_for_domain (GDATA_AUTHORIZER (authorizer),
	                                                     gdata_youtube_service_get_primary_authorization_domain ()) == FALSE);

	g_object_unref (authorizer);

	uhm_server_stop (mock_server);
	g_signal_handler_disconnect (mock_server, handler_id);
}

const gchar *
uhm_server_get_args_help (void)
{
	return "  -t, --trace-dir [directory]    Read/Write trace files in the specified directory\n"
	       "  -w, --write-traces             Work online and write trace files to --trace-dir\n"
	       "  -c, --compare-traces           Work online and compare with existing trace files in --trace-dir\n";
}

/* TODO: Note: Need to override --help as well. */
void
uhm_server_parse_args (int *argc_p, char **argv_p[])
{
	guint argc = *argc_p;
	gchar **argv = *argv_p;
	guint i, e;

	GFile *trace_dir = NULL;
	gboolean write_traces = FALSE;
	gboolean compare_traces = FALSE;

	/* Parse the custom options */
	for (i = 1; i < argc; i++) {
		if (strcmp ("--trace-dir", argv[i]) == 0 || strcmp ("-t", argv[i]) == 0) {
			if (i >= argc - 1) {
				fprintf (stderr, "Error: Missing directory for --trace-dir option.\n");
				exit (1);
			}

			trace_dir = g_file_new_for_path (argv[i + 1]);

			argv[i] = (char *) NULL;
			argv[i + 1] = (char*) NULL;
			i++;
		} else if (strcmp ("--write-traces", argv[i]) == 0 || strcmp ("-w", argv[i]) == 0) {
			write_traces = TRUE;
			argv[i] = (char *) NULL;
		} else if (strcmp ("--compare-traces", argv[i]) == 0 || strcmp ("-c", argv[i]) == 0) {
			compare_traces = TRUE;
			argv[i] = (char *) NULL;
		}
	}

	/* --[write|compare]-traces are mutually exclusive. */
	if (write_traces == TRUE && compare_traces == TRUE) {
		fprintf (stderr, "Error: --write-traces and --compare-traces are mutually exclusive.\n");
		exit (1);
	}

	/* Collapse NULL entries in argv. Code pinched from GLib’s g_test_init():
	 * Copyright (C) 2007 Imendio AB
	 * Authors: Tim Janik, Sven Herzberg */
	e = 1;
	for (i = 1; i < argc; i++) {
		if (argv[i] != NULL) {
			argv[e++] = argv[i];
			if (i >= e)
				argv[i] = NULL;
		}
	}
	*argc_p = e;
}

int
main (int argc, char *argv[])
{
	gint retval;
	GFile *trace_directory;
	gint i;
	SoupSession *session;
	SoupLogger *logger;

	const gchar *expected_domain_names[] = {
		"www.google.com",
		"gdata.youtube.com",
		"uploads.gdata.youtube.com",
		"lh3.googleusercontent.com",
		"lh5.googleusercontent.com",
		"lh6.googleusercontent.com",
		NULL;
	};

#if !GLIB_CHECK_VERSION (2, 35, 0)
	g_type_init ();
#endif

	/* TODO: This is ugly and doesn’t actually set the options on the
	 * UhmServer. How about a GOptionContext-based thing which gets called
	 * after g_test_init()? Doesn’t matter that it’s GOptionContext-based
	 * because uhttpmock is never going to be used to test that. */
	uhm_server_parse_args (&argc, &argv);
	g_test_init (&argc, &argv, NULL);

	/* Set up the client. */
	session = soup_session_new_with_options ("ssl-strict", FALSE,
	                                         "timeout", 0,
	                                         NULL);

	soup_session_add_feature_by_type (session, SOUP_TYPE_PROXY_RESOLVER_DEFAULT);

	/* Log all libsoup traffic if debugging's turned on */
	logger = soup_logger_new (SOUP_LOGGER_LOG_BODY, -1);
	soup_logger_set_printer (logger, uhm_server_received_message_chunk_from_soup, g_object_ref (mock_server), g_object_unref);
	soup_session_add_feature (session, SOUP_SESSION_FEATURE (logger));
	g_object_unref (logger);

	/* Set up the mock server. */
	mock_server = uhm_server_new ();
	uhm_server_set_enable_logging (mock_server, write_traces);
	uhm_server_set_enable_online (mock_server, write_traces || compare_traces);

	/* Configure uhttpmock to use SSL. */
	uhm_server_set_default_tls_certificate (mock_server);

	/* Set up the expected domain names. */
	uhm_server_set_expected_domain_names (mock_server, expected_domain_names);

	/* Set up the trace directory. */
	trace_directory = g_file_new_for_path (TEST_FILE_DIR "traces/youtube");
	uhm_server_set_trace_directory (mock_server, trace_directory);
	g_object_unref (trace_directory);

	/* Register the tests. */
	g_test_add_func ("/youtube/authentication", test_authentication);
	g_test_add_func ("/youtube/authentication/error", test_authentication_error);
	g_test_add_func ("/youtube/authentication/timeout", test_authentication_timeout);

	retval = g_test_run ();

	return retval;
}
