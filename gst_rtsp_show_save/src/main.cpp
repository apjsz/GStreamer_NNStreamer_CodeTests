/*
 * Parts are taken from https://www.it-jim.com/ other parts from the documentation.
 *
 * Implementaré:
 *
 gst-launch-1.0 rtspsrc location= \
 "rtsp://video:<password@ip_address:port>/cam/realmonitor?channel=1&subtype=0&unicast=true&proto=Onvif" \
 latency=200 ! queue ! rtph265depay ! h265parse ! avdec_h265 ! videoconvert ! tee name=t \
 t. ! queue leaky=2 max-size-buffers=10 ! videoscale \
 ! capsfilter caps=video/x-raw,width=800,height=450 ! autovideosink \
 t. ! queue leaky=2 max-size-buffers=10 ! videoscale \
 ! capsfilter caps=video/x-raw,width=320,height=180 ! autovideosink
 *
 */

#include <iostream>
#include <string>

#include <glib.h>
#include <gst/gst.h>
#include <gst/video/navigation.h>

#define VIDEOSINK 1

typedef struct rtspPipeline {
	GstElement *pipeline;
	GstElement *rtspsrc;
	GstElement *queue;
	GstElement *depay;
	GstElement *parse;
	GstElement *avdev;
	GstElement *vidconv;
	GstElement *tee;

	GstElement *queue1;
	GstElement *vidscale1;
	GstElement *capsfilt1;
	GstElement *vidsink1;
	GstElement *queue2;
	GstElement *vidscale2;
	GstElement *capsfilt2;
	GstElement *vidsink2;

	GstElement *encoder;
	GstElement *muxer;
	GstElement *filesink;
} rtspPipeline_t;

rtspPipeline_t data;


//======================================================================================================================
/// A simple assertion function
/// Never use C++ assert statement, the whole line will be removed in Release builds !
inline void myAssert(bool b, const std::string &s = "MYASSERT ERROR !") {
	if (!b)
		throw std::runtime_error(s);
}

/// And the macro version, requires true or anything non-zero (converted to bool true) to pass
/// This is similar to CV_Assert() of OPenCV
#define MY_ASSERT(x) myAssert(x, "MYASSERT ERROR :" #x)

//======================================================================================================================
/// Check GStreamer error, exit on error
inline void checkErr(GError *err) {
	if (err) {
		std::cerr << "checkErr : " << err->message << std::endl;
		exit(0);
	}
}

//======================================================================================================================
void diagnose(GstElement *element);

/// Process a single bus message, log messages, exit on error, return false on eof
/// This is a MODIFIED version, we run diagnose in here !
/// Diagnose would not give much info before we actually start playing the pipeline (PAUSED is enough)
static bool busProcessMsg(GstElement *pipeline, GstMessage *msg,
		const std::string &prefix, GstElement *elemToDiagnose) {
	using namespace std;

	GstMessageType mType = GST_MESSAGE_TYPE(msg);
	cout << "[" << prefix << "] : mType = " << mType << " ";
	switch (mType) {
	case (GST_MESSAGE_ERROR):
		// Parse error and exit program, hard exit
		GError *err;
		gchar *dbg;
		gst_message_parse_error(msg, &err, &dbg);
		cout << "ERR = " << err->message << " FROM "
				<< GST_OBJECT_NAME(msg->src) << endl;
		cout << "DBG = " << dbg << endl;
		g_clear_error(&err);
		g_free(dbg);
		exit(1);
	case (GST_MESSAGE_EOS):
		// Soft exit on EOS
		cout << " EOS !" << endl;
		return false;
	case (GST_MESSAGE_STATE_CHANGED):
		// Parse state change, print extra info for pipeline only
		cout << "State changed !" << endl;
		if (GST_MESSAGE_SRC(msg) == GST_OBJECT(pipeline)) {
			GstState sOld, sNew, sPenging;
			gst_message_parse_state_changed(msg, &sOld, &sNew, &sPenging);
			cout << "Pipeline changed from " << gst_element_state_get_name(sOld)
					<< " to " << gst_element_state_get_name(sNew) << endl;
			diagnose(elemToDiagnose);
		}
		break;
	case (GST_MESSAGE_STEP_START):
		cout << "STEP START !" << endl;
		break;
	case (GST_MESSAGE_STREAM_STATUS):
		cout << "STREAM STATUS !" << endl;
		break;
	case (GST_MESSAGE_ELEMENT): {
		cout << "MESSAGE ELEMENT !" << endl;

		// You can add more stuff here if you want

		GstEvent *evt;
		const char *key;
		if (gst_navigation_message_get_type(msg)
				!= GST_NAVIGATION_MESSAGE_EVENT)
			break;
		if (!gst_navigation_message_parse_event(msg, &evt))
			break;
		if (gst_navigation_event_get_type(evt)
				!= GST_NAVIGATION_EVENT_KEY_PRESS)
			break;
		gst_navigation_event_parse_key_event(evt, &key);
		if (g_ascii_strcasecmp(key, "space") == 0)
			std::cout << "Space" << std::endl;
		else if (g_ascii_strcasecmp(key, "e") == 0) {
			//gst_element_post_message_eos(data.pipeline, (GST_OBJECT(data.pipeline)));
			gst_element_send_event (pipeline, gst_event_new_eos());
		}

		/*
		 if (g_ascii_strcasecmp(key, "space") == 0)
		 toggle_pause(pipeline);
		 else if (g_ascii_strcasecmp(key, "right") == 0)
		 //seek(pipeline, 5 * GST_SECOND);
		 else if (g_ascii_strcasecmp(key, "left") == 0)
		 seek(pipeline, -5 * GST_SECOND);
		 else if (g_ascii_strcasecmp(key, "f") == 0)
		 speed(pipeline, 2.);
		 else if (g_ascii_strcasecmp(key, "r") == 0)
		 speed(pipeline, -2.);
		 else if (g_ascii_strcasecmp(key, "n") == 0)
		 speed(pipeline, 1.);
		 */
		else if (g_ascii_strcasecmp(key, "q") == 0) {
			std::cout << "Press Q" << std::endl;
			return false;
		}
		break;
	}
	default:
		cout << endl;
	}
	return true;
}

//======================================================================================================================
// A few useful routines for diagnostics

static gboolean printField(GQuark field, const GValue *value, gpointer pfx) {
	using namespace std;
	gchar *str = gst_value_serialize(value);
	cout << (char*) pfx << " " << g_quark_to_string(field) << " " << str
			<< endl;
	g_free(str);
	return TRUE;
}

void printCaps(const GstCaps *caps, const std::string &pfx) {
	using namespace std;
	if (caps == nullptr)
		return;
	if (gst_caps_is_any(caps))
		cout << pfx << "ANY" << endl;
	else if (gst_caps_is_empty(caps))
		cout << pfx << "EMPTY" << endl;
	for (int i = 0; i < gst_caps_get_size(caps); ++i) {
		GstStructure *s = gst_caps_get_structure(caps, i);
		cout << pfx << gst_structure_get_name(s) << endl;
		gst_structure_foreach(s, &printField, (gpointer) pfx.c_str());
	}
}

void printPadsCB(const GValue *item, gpointer userData) {
	using namespace std;
	GstElement *element = (GstElement*) userData;
	GstPad *pad = (GstPad*) g_value_get_object(item);
	myAssert(pad);
	cout << "PAD : " << gst_pad_get_name(pad) << endl;
	GstCaps *caps = gst_pad_get_current_caps(pad);
	char *str = gst_caps_to_string(caps);
	cout << str << endl;
	free(str);
}

void printPads(GstElement *element) {
	using namespace std;
	GstIterator *pad_iter = gst_element_iterate_pads(element);
	gst_iterator_foreach(pad_iter, printPadsCB, element);
	gst_iterator_free(pad_iter);

}

void diagnose(GstElement *element) {
	using namespace std;
	cout << "=====================================" << endl;
	cout << "DIAGNOSE element : " << gst_element_get_name(element) << endl;
	printPads(element);
	cout << "=====================================" << endl;
}

static void pad_added_cb(GstElement *src, GstPad *new_pad, rtspPipeline_t *pThis)
{
	GstPad *sink_pad = gst_element_get_static_pad(pThis->queue, "sink");
	GstPadLinkReturn ret;
	GstCaps *new_pad_caps = NULL;
	GstStructure *new_pad_struct = NULL;
	const gchar *new_pad_type = NULL;

	GstCaps *caps;
	char *str;

	g_print("Received new pad '%s' from '%s':\n", GST_PAD_NAME(new_pad), GST_ELEMENT_NAME(src));

	/* Check the new pad's name */
	if (!g_str_has_prefix(GST_PAD_NAME(new_pad), "recv_rtp_src_")) {
		g_print("  It is not the right pad.  Need recv_rtp_src_. Ignoring.\n");
		goto exit;
	}

	/* If our converter is already linked, we have nothing to do here */
	if (gst_pad_is_linked(sink_pad)) {
		g_print(" Sink pad from %s already linked. Ignoring.\n", GST_ELEMENT_NAME(src));
		goto exit;
	}

	/* Check the new pad's type */
	new_pad_caps = gst_pad_query_caps(new_pad, NULL);
	new_pad_struct = gst_caps_get_structure(new_pad_caps, 0);
	new_pad_type = gst_structure_get_name(new_pad_struct);

	g_print("New pad type: %s\n", new_pad_type);

	caps = gst_pad_get_current_caps(new_pad);
	str = gst_caps_to_string(caps);
	g_print("Caps del pad: %s\n", str);

	/* Attempt the link */
	ret = gst_pad_link(new_pad, sink_pad);
	if (GST_PAD_LINK_FAILED(ret)) {
		g_print("  Type is '%s' but link failed.\n", new_pad_type);
	}
	else {
		g_print("  Link succeeded (type '%s').\n", new_pad_type);
	}

exit:
	/* Unreference the new pad's caps, if we got them */
	if (new_pad_caps != NULL)
		gst_caps_unref(new_pad_caps);

	/* Unreference the sink pad */
	gst_object_unref(sink_pad);
}

/*
 * Modifico el main para incorporar las hebras de C++ y comandos por linea de comando.
 */

int main(int argc, char *argv[]) {
	std::cout << "Ensayo de gstreamer con procesamiento." << std::endl;

	gint width1 = 800;
	gint height1 = 450;
	gint width2 = 1920;
	gint height2 = 1080;
	GstBus *bus;
	GstMessage *msg;
	GstStateChangeReturn ret;
	GstPad *tee_pad1, *tee_pad2;
	GstPad *queue_pad1, *queue_pad2;

	gst_init(&argc, &argv);

	data.pipeline = gst_pipeline_new("pipeline");

	//data.pipeline = gst_element_factory_make("pipeline", "pipeline");
	data.rtspsrc = gst_element_factory_make("rtspsrc", "rtspsrc");

	// Set up parameters
	std::string rtspstr =
			"rtsp://video:<password@ip_address:port>/cam/realmonitor?channel=1&subtype=0&unicast=true&proto=Onvif";
	std::cout << rtspstr << std::endl;

	//gst_element_link()
	g_object_set(data.rtspsrc, "location", rtspstr.c_str(), nullptr);
	g_object_set(data.rtspsrc, "latency", 10, nullptr);

	data.queue = gst_element_factory_make("queue", "queue");
	data.depay = gst_element_factory_make("rtph265depay", "depay");
	data.parse = gst_element_factory_make("h265parse", "parse");
	data.avdev = gst_element_factory_make("avdec_h265", "avdec");
	data.vidconv = gst_element_factory_make("videoconvert", "vidconv");
	data.tee = gst_element_factory_make("tee", "tee");

	data.queue1 = gst_element_factory_make("queue", "queue1");
	g_object_set (data.queue1, "leaky", 2, "max-size-buffers", 10, NULL);
	data.vidscale1 = gst_element_factory_make("videoscale", "vidscale1");
	data.capsfilt1 = gst_element_factory_make("capsfilter", "capsfilt1");
	data.vidsink1 = gst_element_factory_make("autovideosink", "vidsink1");
	g_object_set(data.vidsink1, "sync", "false", NULL);
	data.queue2 = gst_element_factory_make("queue", "queue2");
	g_object_set (data.queue2, "leaky", 2, "max-size-buffers", 10, NULL);
	data.vidscale2 = gst_element_factory_make("videoscale", "vidscale2");
	data.capsfilt2 = gst_element_factory_make("capsfilter", "capsfilt2");
	data.vidsink2 = gst_element_factory_make("autovideosink", "vidsink2");

	data.encoder = gst_element_factory_make("x264enc", "encoder");
	data.muxer = gst_element_factory_make("matroskamux", "muxer");
	data.filesink = gst_element_factory_make("filesink", "filesink");
	g_object_set(data.filesink, "location", "ensayo.mkv", NULL);

	// Pipeline previo a la tee.

	MY_ASSERT(data.pipeline && data.rtspsrc && data.queue && data.depay && data.parse
			&& data.avdev && data.vidconv && data.tee);

	if (VIDEOSINK) {
		MY_ASSERT(data.vidscale1 && data.capsfilt1 && data.vidsink1 &&
			data.vidscale2 && data.capsfilt2 && data.vidsink2);
	}
	else {
		MY_ASSERT(data.vidscale1 && data.capsfilt1 && data.vidsink1 &&
			data.vidscale2 && data.capsfilt2 && data.encoder && data.muxer && data.filesink);
	}

	// Add and link elements
	gst_bin_add_many(GST_BIN(data.pipeline), data.rtspsrc, data.queue, data.depay, data.parse,
			data.avdev, data.vidconv, data.tee, nullptr);
	// Rama 1 de la tee
	gst_bin_add_many(GST_BIN(data.pipeline), data.queue1, data.vidscale1, data.capsfilt1, data.vidsink1, nullptr);

	// Rama 2 de la tee
	if (VIDEOSINK) {
		gst_bin_add_many(GST_BIN(data.pipeline), data.queue2, data.vidscale2, data.capsfilt2, data.vidsink2, nullptr);
	}
	else {
		gst_bin_add_many(GST_BIN(data.pipeline), data.queue2, data.vidscale2, data.capsfilt2,
			data.encoder, data.muxer, data.filesink, nullptr);
	}

	MY_ASSERT(gst_element_link_many(data.queue, data.depay, data.parse, data.avdev,	data.vidconv, data.tee, nullptr));

	// Vinculo dinámico del pad creado.
	g_signal_connect (data.rtspsrc, "pad-added", G_CALLBACK (pad_added_cb), &data);

	// Creo las dos ramas del pipeline.
	std::string str;
	GstCaps *vidscalecaps;

	str = "video/x-raw,width=" + std::to_string(width1) + ",height=" + std::to_string(height1);
	std::cout << str << std::endl;
	vidscalecaps = gst_caps_from_string(str.c_str());
	g_object_set(G_OBJECT(data.capsfilt1), "caps", vidscalecaps, nullptr);
	gst_caps_unref(vidscalecaps);

	str = "video/x-raw,width=" + std::to_string(width2) + ",height=" + std::to_string(height2);
	std::cout << str << std::endl;
	vidscalecaps = gst_caps_from_string(str.c_str());
	g_object_set(G_OBJECT(data.capsfilt2), "caps", vidscalecaps, nullptr);
	gst_caps_unref(vidscalecaps);

	MY_ASSERT(gst_element_link_many(data.queue1, data.vidscale1, data.capsfilt1, data.vidsink1, nullptr));
	tee_pad1 = gst_element_request_pad_simple(data.tee, "src_%u");
	g_print("Obtained request pad %s for video pad 1.\n", gst_pad_get_name(tee_pad1));
	queue_pad1 = gst_element_get_static_pad(data.queue1, "sink");

	if (VIDEOSINK) {
		MY_ASSERT(gst_element_link_many(data.queue2, data.vidscale2, data.capsfilt2, data.vidsink2, nullptr));
	}
	else {
		MY_ASSERT(gst_element_link_many(data.queue2, data.vidscale2, data.capsfilt2, data.encoder, data.muxer, data.filesink, nullptr));
	}
	tee_pad2= gst_element_request_pad_simple(data.tee, "src_%u");
	g_print("Obtained request pad %s for video pad 2.\n", gst_pad_get_name(tee_pad2));
	queue_pad2 = gst_element_get_static_pad(data.queue2, "sink");

	if (gst_pad_link (tee_pad1, queue_pad1) != GST_PAD_LINK_OK) {
		std::cout << "Error enlaze tee 1" << std::endl;
	}
	if (gst_pad_link (tee_pad2, queue_pad2) != GST_PAD_LINK_OK) {
		std::cout << "Error enlaze tee 2" << std::endl;
	}

	gst_object_unref (queue_pad1);
	gst_object_unref (queue_pad2);

	GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN (data.pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "archivo");

	// Start playing
	ret = gst_element_set_state(data.pipeline, GST_STATE_PLAYING);
	if (ret == GST_STATE_CHANGE_FAILURE) {
		g_printerr("Unable to set the pipeline to the playing state.\n");
		gst_object_unref(data.pipeline);
		return -1;
	}

	// Message - processing loop
	bus = gst_element_get_bus(data.pipeline);
	for (;;) {
		// Wait for message, no filtering, any message goes
		msg = gst_bus_timed_pop(bus, GST_CLOCK_TIME_NONE);
		GstElement *conv;

		bool res = busProcessMsg(data.pipeline, msg, "GOBLIN", conv);
		gst_message_unref(msg);
		if (!res)
			break;
	}
	gst_object_unref(bus);

	// Free resources
	gst_object_unref(bus);
	gst_element_set_state(data.pipeline, GST_STATE_NULL);
	gst_object_unref(data.pipeline);

	return 0;
}
