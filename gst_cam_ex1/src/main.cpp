#include <iostream>
#include <string>

#include <gst/gst.h>

/* Implemento esto inicialmente:

   gst-launch-1.0 v4l2src ! videoconvert ! tee name=t \
	t. ! queue leaky=2 max-size-buffers=10 \
	! videoscale ! video/x-raw,width=640,height=480,framerate=30/1 ! autovideosink \
	t. ! queue leaky=2 max-size-buffers=10 \
	! videoscale ! video/x-raw,width=1280,height=720,framerate=30/1 ! autovideosink
 */

static gboolean link_elements_with_filter2(GstElement *element1, GstElement *element2, gint width, gint height) {
	gboolean link_ok;
	GstCaps *caps;
	std::string str;
	const char *c;

 	str = "video/x-raw, width=" + std::to_string(width) + ",height=" + std::to_string(height) + ",framerate=30/1";
 	c = str.c_str();
 	std::cout <<  c << std::endl;

	//caps = gst_caps_from_string("video/x-raw,width=640,height=480,framerate=30/1");
	caps = gst_caps_from_string(c);

	link_ok = gst_element_link_filtered(element1, element2, caps);
	gst_caps_unref(caps);

	if (!link_ok) {
		g_warning("Failed to link element1 and element2");
	}

	return link_ok;
}

int main (int argc, char *argv[]) {
	std::cout << "Ensayo de gstreamer con procesamiento." << std::endl;

	GstElement *pipeline;
	GstElement *tee;
	GstElement *source;
	GstElement *convert;
	GstElement *queue_vid1;
	GstElement *queue_vid2;
	GstElement *scale_vid1;
	GstElement *scale_vid2;
	GstElement *sink_vid1;
	GstElement *sink_vid2;
	GstBus *bus;
	GstMessage *msg;
	GstStateChangeReturn ret;
	GstPad *tee_pad_vid1, *tee_pad_vid2;
	GstPad *queue_pad_vid1, *queue_pad_vid2;

	gst_init (&argc, &argv);

	tee = gst_element_factory_make ("tee", "tee");
	source = gst_element_factory_make ("v4l2src", "video-src");
	g_object_set (source, "device", "/dev/video0", NULL);			// Seteo el video a usar
	convert = gst_element_factory_make ("videoconvert", "convert");
	queue_vid1 = gst_element_factory_make ("queue", "queue-vid1");
	g_object_set (queue_vid1, "leaky", 2, "max-size-buffers", 10, NULL);
	queue_vid2 = gst_element_factory_make ("queue", "queue-vid2");
	g_object_set (queue_vid2, "leaky", 2, "max-size-buffers", 10, NULL);
	scale_vid1 = gst_element_factory_make ("videoscale", "scale-vid1");
	scale_vid2 = gst_element_factory_make ("videoscale", "scale-vid2");
	sink_vid1 = gst_element_factory_make ("autovideosink", "sink-vid1");
	sink_vid2 = gst_element_factory_make ("autovideosink", "sink-vid2");

	pipeline = gst_pipeline_new ("pipeline");

	if (!pipeline || !tee || !source || !convert || !queue_vid1 || !queue_vid2 ||
			!scale_vid1 || !scale_vid2 || !sink_vid1 || !sink_vid2) {
		std::cout << "Error al crear alguno de los elementos" << std::endl;
		return -1;
	}

	gst_bin_add_many(GST_BIN (pipeline), source, tee, convert, queue_vid1,
			queue_vid2, scale_vid1, scale_vid2, sink_vid1, sink_vid2, NULL);

	// Link the elements into the pipeline
	if ((gst_element_link_many (source, convert, tee, NULL) != TRUE) ||
			(gst_element_link_many (queue_vid1, scale_vid1, NULL) != TRUE) ||
			(link_elements_with_filter2 (scale_vid1, sink_vid1, 800, 600) != TRUE) ||
			(gst_element_link_many (queue_vid2, scale_vid2, NULL) != TRUE) ||
			(link_elements_with_filter2 (scale_vid2, sink_vid2, 320, 200) != TRUE)) {
		std::cout << "Linking error" << std::endl;
		gst_object_unref (pipeline);
		return -1;
	}

	tee_pad_vid1 = gst_element_request_pad_simple (tee, "src_%u");
	g_print("Obtained request pad %s for video pad 1.\n", gst_pad_get_name(tee_pad_vid1));
	queue_pad_vid1 = gst_element_get_static_pad (queue_vid1, "sink");

	tee_pad_vid2 = gst_element_request_pad_simple (tee, "src_%u");
	g_print("Obtained request pad %s for video pad 2.\n", gst_pad_get_name(tee_pad_vid2));
	queue_pad_vid2 = gst_element_get_static_pad (queue_vid2, "sink");

	if (gst_pad_link (tee_pad_vid1, queue_pad_vid1) != GST_PAD_LINK_OK) {
		std::cout << "Error enlaze tee 1" << std::endl;
	}
	if (gst_pad_link (tee_pad_vid2, queue_pad_vid2) != GST_PAD_LINK_OK) {
		std::cout << "Error enlaze tee 2" << std::endl;
	}

	gst_object_unref (queue_pad_vid1);
	gst_object_unref (queue_pad_vid2);

	GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN (pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "dot-file");

	  // Start playing
	  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
	  if (ret == GST_STATE_CHANGE_FAILURE) {
	    g_printerr ("Unable to set the pipeline to the playing state.\n");
	    gst_object_unref (pipeline);
	    return -1;
	  }

	  // Wait until error or EOS
	  bus = gst_element_get_bus (pipeline);
	  msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE, GstMessageType (GST_MESSAGE_ERROR | GST_MESSAGE_EOS));

	  // Parse message
	  if (msg != NULL) {
	    GError *err;
	    gchar *debug_info;

	    switch (GST_MESSAGE_TYPE (msg)) {
	      case GST_MESSAGE_ERROR:
	        gst_message_parse_error (msg, &err, &debug_info);
	        g_printerr ("Error received from element %s: %s\n",
	            GST_OBJECT_NAME (msg->src), err->message);
	        g_printerr ("Debugging information: %s\n",
	            debug_info ? debug_info : "none");
	        g_clear_error (&err);
	        g_free (debug_info);
	        break;
	      case GST_MESSAGE_EOS:
	        g_print ("End-Of-Stream reached.\n");
	        break;
	      default:
	        // We should not reach here because we only asked for ERRORs and EOS
	        g_printerr ("Unexpected message received.\n");
	        break;
	    }
	    gst_message_unref (msg);
	  }

	  // Free resources
	  gst_object_unref (bus);
	  gst_element_set_state (pipeline, GST_STATE_NULL);
	  gst_object_unref (pipeline);

	return 0;
}
