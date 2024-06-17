#!/usr/bin/env python

"""
@file		nnstreamer_ex1_movenet_tflite.py
@date		17 april 2024
@brief		Tensor stream example with filter
@see		https://github.com/nnsuite/nnstreamer
@see        https://github.com/nxp-imx/nxp-nnstreamer-examples
@author		
@bug		No known bugs.

Pipeline que reproduce rtsp desde la cámara de vigilancia.

gst-launch-1.0 -v rtspsrc location="rtsp://video:<password@ip_address:port>/cam/realmonitor?\
channel=1&subtype=0&unicast=true&proto=Onvif" latency=200 \
! rtph265depay ! h265parse ! avdec_h265 ! videoscale ! videoconvert \
! videoconvert ! capsfilter caps=video/x-raw,width=1920,height=1080 \
! cairooverlay name=cairooverlay ! ximagesink name=img_tensor    


NNStreamer example for pose estimation using tensorflow-lite.

Pipeline :

filesrc (o videosrc)--convert--tee--queue--convert--tensor_convert--tensor_transform--tensor_filter--tensor_sink
				|
				---queue--videoconvert--cairooverlay--ximagesink           

This app displays video sink.

'tensor_filter' for pose estimation.
The model used is movenet_thunder_i8.tflite.
Change the model and the input width and height to check others.

movenet_thunder:
    input (256,256,3)
    output (1,1,17,3) quantized to [0.0, 1.0] where there are 17 points and
    each point is defined as (y, x, score)
    Score is the probability that the detected point is a joint.

'tensor_sink' updates the pose points and lines to display in textoverlay.

Run example :
Before running this example, GST_PLUGIN_PATH should be updated for nnstreamer plugin.
$ export GST_PLUGIN_PATH=$GST_PLUGIN_PATH:<nnstreamer plugin path>
$ python nnstreamer_ex1_movenet_tflite.py

See https://lazka.github.io/pgi-docs/#Gst-1.0 for Gst API details.
"""

import os
import sys
import logging
import cairo
import gi
import numpy as np
import datetime
import math
import signal

gi.require_version('Gst', '1.0')
from gi.repository import Gst, GObject, GLib

# logging.warning('Watch out!')
Gst.debug_set_active(True)
Gst.debug_set_default_threshold(2)


class NNStreamerExample:
    """NNStreamer example for image classification."""

    def __init__(self, argv=None):
        # las dimensiones del video ingresarán en init
        self.VIDEO_INPUT_WIDTH = 640
        self.VIDEO_INPUT_HEIGHT = 480
        
        self.VIDEO_INPUT_CROPPED_WIDTH = 640
        self.VIDEO_INPUT_CROPPED_HEIGHT = 480
        
        # model constants
        self.MODEL_KEYPOINT_SIZE = 17 
        self.MODEL_INPUT_HEIGHT = 256
        self.MODEL_INPUT_WIDTH = 256
        self.MODEL_KEYPOINT_INDEX_Y = 0
        self.MODEL_KEYPOINT_INDEX_X = 1
        self.MODEL_KEYPOINT_INDEX_SCORE = 2
        self.MODEL_SCORE_THRESHOLD = 0.11
        
        self.current_label_index = -1
        self.new_label_index = -1
        self.tflite_model = ''
        self.tflite_labels = []

        # pipeline variables
        self.mainloop = None
        self.pipeline = None
        self.running = False
        self.time = datetime.datetime.now()
        self.fps = 0
        self.backend = None
        self.source = None

        self.tflite_path = None
        self.video_path = None
        # self.video_file = video_file
        self.np_kpts = None
        self.tensor_transform = None
        self.tensor_filter_custom = None

        # keypoints definition
        # https://github.com/tensorflow/tfjs-models/tree/master/pose-detection#keypoint-diagram
        self.keypoints_def = [
            {'label': 'nose', 'connections': [1, 2, ]},
            {'label': 'left_eye', 'connections': [0, 3, ]},
            {'label': 'right_eye', 'connections': [0, 4, ]},
            {'label': 'left_ear', 'connections': [1, ]},
            {'label': 'right_ear', 'connections': [2, ]},
            {'label': 'left_shoulder', 'connections': [6, 7, 11, ]},
            {'label': 'right_shoulder', 'connections': [5, 8, 12, ]},
            {'label': 'left_elbow', 'connections': [5, 9, ]},
            {'label': 'right_elbow', 'connections': [6, 10, ]},
            {'label': 'left_wrist', 'connections': [7, ]},
            {'label': 'right_wrist', 'connections': [8, ]},
            {'label': 'left_hip', 'connections': [5, 12, 13, ]},
            {'label': 'right_hip', 'connections': [6, 11, 14, ]},
            {'label': 'left_knee', 'connections': [11, 15, ]},
            {'label': 'right_knee', 'connections': [12, 16, ]},
            {'label': 'left_ankle', 'connections': [13, ]},
            {'label': 'right_ankle', 'connections': [14, ]},
        ]

        assert len(self.keypoints_def) == self.MODEL_KEYPOINT_SIZE
   
        if not self.tflite_init():
            raise Exception

        GObject.threads_init()
        
        signal.signal(signal.SIGINT, self.sigint_handler)
        
        Gst.init(argv)

    def run_example(self):
        """Init pipeline and run example.

        :return: None
        """
        # main loop
        self.mainloop = GObject.MainLoop()
      
        # Agregaré el algoritmo de detección de pose en en rtsp de la cámara de vigilancia.
        # init pipeline
        strPipe = 'rtspsrc location=rtsp://video:password123@192.168.0.151:554/cam/realmonitor?'\
            'channel=1&subtype=0&unicast=true&proto=Onvif latency=200 '
        strPipe += '! rtph265depay ! h265parse ! avdec_h265 ! videoconvert '
        strPipe += '! videoscale ! capsfilter caps=video/x-raw,width={:d},height={:d} ' \
            .format(self.VIDEO_INPUT_WIDTH, self.VIDEO_INPUT_HEIGHT)
        strPipe += '! tee name=t_raw '
        strPipe += 't_raw. ! queue leaky=2 max-size-buffers=2 '
        strPipe += '! videoconvert ! cairooverlay name= cairooverlay ! ximagesink name=img_tensor '
        strPipe += 't_raw. ! queue leaky=2 max-size-buffers=2 ! videoscale '
        strPipe += '! video/x-raw,width={:d},height={:d},format=RGB ' \
            .format(self.MODEL_INPUT_WIDTH, self.MODEL_INPUT_HEIGHT)
        strPipe += '! tensor_converter '
        strPipe += '! tensor_filter framework=tensorflow-lite model={:s} ' \
            .format(self.tflite_model)
        strPipe += '! tensor_sink name=tensor_sink'
                
        print("\n Str Pipe: ", strPipe,"\n")

        self.pipeline = Gst.parse_launch(strPipe)

        Gst.debug_bin_to_dot_file(self.pipeline, Gst.DebugGraphDetails.ALL, "pipeline")

        # bus and message callback
        bus = self.pipeline.get_bus()
        bus.add_signal_watch()
        bus.connect('message', self.on_bus_message)

        # tensor sink signal : new data callback
        tensor_sink = self.pipeline.get_by_name('tensor_sink')
        tensor_sink.connect('new-data', self.new_data_cb)
        
        tensor_res = self.pipeline.get_by_name('cairooverlay')
        tensor_res.connect('draw', self.draw_cb)
        
        # timer to update result
        # GObject.timeout_add(500, self.on_timer_update_result)

        # start pipeline
        self.pipeline.set_state(Gst.State.PLAYING)
        self.running = True

        GLib.timeout_add(100, self.timeout_function)

        # set window title
        self.set_window_title('img_tensor', 'NNStreamer Pose Example')

        # run main loop
        self.mainloop.run()

        # quit when received eos or error message
        self.running = False
        self.pipeline.set_state(Gst.State.NULL)

        bus.remove_signal_watch()

    def on_bus_message(self, bus, message):
        """Callback for message.

        :param bus: pipeline bus
        :param message: message from pipeline
        :return: None
        """
        
        if message.type == Gst.MessageType.EOS:
            logging.info('received eos message')
            self.loop.quit()
        elif message.type == Gst.MessageType.ERROR:
            error, debug = message.parse_error()
            logging.warning('[error] %s : %s', error.message, debug)
            self.mainloop.quit()
        elif message.type == Gst.MessageType.WARNING:
            error, debug = message.parse_warning()
            logging.warning('[warning] %s : %s', error.message, debug)
        elif message.type == Gst.MessageType.STREAM_START:
            logging.info('received start message')
        elif message.type == Gst.MessageType.QOS:
            data_format, processed, dropped = message.parse_qos_stats()
            format_str = Gst.Format.get_name(data_format)
            logging.debug('[qos] format[%s] processed[%d] dropped[%d]', format_str, processed, dropped)

    def tflite_init(self):
        """Check tflite model and load labels.

        :return: True if successfully initialized
        """
        tflite_model = 'movenet_thunder_i8.tflite'
        tflite_label = 'labels.txt'
        current_folder = os.path.dirname(os.path.abspath(__file__))
        model_folder = os.path.join(current_folder, 'movenet')

        # check model file exists
        self.tflite_model = os.path.join(model_folder, tflite_model)
        if not os.path.exists(self.tflite_model):
            logging.error('cannot find tflite model [%s]', self.tflite_model)
            return False

        # load labels
        label_path = os.path.join(model_folder, tflite_label)
        try:
            with open(label_path, 'r') as label_file:
                for line in label_file.readlines():
                    self.tflite_labels.append(line)
        except FileNotFoundError:
            logging.error('cannot find tflite label [%s]', label_path)
            return False

        logging.info('finished to load labels, total [%d]', len(self.tflite_labels))
        return True


    def new_data_cb(self, sink, buffer):
        """Callback for tensor sink signal.

        :param sink: tensor sink element
        :param buffer: buffer from element
        :return: None
        """
        if self.running:
            
            if buffer.n_memory() != 1:
                return False
            
            # tensor buffer #0
            mem_kpts = buffer.peek_memory(0)
            result, info = mem_kpts.map(Gst.MapFlags.READ)
            
            if result:
                # convert buffer to [1:1:17:3] numpy array
                    
                np_kpts = np.frombuffer(info.data, dtype=np.float32) \
                    .reshape(17, -1) \
                        .copy()        
                
                # rescale normalized keypoints (x,y) per video resolution
                np_kpts[:, self.MODEL_KEYPOINT_INDEX_X] *= \
                    self.VIDEO_INPUT_CROPPED_WIDTH
                np_kpts[:, self.MODEL_KEYPOINT_INDEX_Y] *= \
                    self.VIDEO_INPUT_CROPPED_HEIGHT

                # score confidence criteria
                for np_kpt in np_kpts:
                    score = np_kpt[self.MODEL_KEYPOINT_INDEX_SCORE]
                    valid = (score >= self.MODEL_SCORE_THRESHOLD)
                    np_kpt[self.MODEL_KEYPOINT_INDEX_SCORE] = valid

                self.np_kpts = np_kpts
                
            # print(self.np_kpts)

            mem_kpts.unmap(info)

            # coarse fps computation
            now = datetime.datetime.now()
            delta_ms = (now - self.time).total_seconds() * 1000
            self.time = now
            self.fps = 1000 / delta_ms
            
            # print("FPS:", self.fps)
    
    def draw_cb(self, overlay, context, timestamp, duration):
        if self.np_kpts is None or not self.running:
            return

        context.select_font_face('Arial', cairo.FONT_SLANT_NORMAL, cairo.FONT_WEIGHT_NORMAL)
        context.set_line_width(1.0)

        for i in range(0, self.MODEL_KEYPOINT_SIZE):

            # np_pkts access is GIL protected
            np_kpt = self.np_kpts[i]
            valid = np_kpt[self.MODEL_KEYPOINT_INDEX_SCORE]
            if not valid:
                continue

            keypoint_def = self.keypoints_def[i]

            x_kpt = np_kpt[self.MODEL_KEYPOINT_INDEX_X]
            y_kpt = np_kpt[self.MODEL_KEYPOINT_INDEX_Y]

            # draw keypoint spot
            context.set_source_rgb(1, 0, 0)
            context.arc(x_kpt, y_kpt, 1, 0, 2 * math.pi)
            context.fill()
            context.stroke()

            # draw keypoint label
            context.set_source_rgb(0, 1, 1)
            context.set_font_size(10.0)
            context.move_to(x_kpt + 5, y_kpt + 5)
            label = keypoint_def['label']
            context.show_text(label)

            # draw keypoint connections
            context.set_source_rgb(0, 1, 0)
            connections = keypoint_def['connections']
            for connect in connections:
                np_connect = self.np_kpts[connect]
                valid = np_connect[self.MODEL_KEYPOINT_INDEX_SCORE]
                if not valid:
                    continue

                x_connect = np_connect[self.MODEL_KEYPOINT_INDEX_X]
                y_connect = np_connect[self.MODEL_KEYPOINT_INDEX_Y]

                context.move_to(x_kpt, y_kpt)
                context.line_to(x_connect, y_connect)
            context.stroke()

            # display fps indication
            context.set_source_rgb(0.85, 0, 1)
            context.move_to(14, 14)
            context.select_font_face('Arial',
                                     cairo.FONT_SLANT_NORMAL, cairo.FONT_WEIGHT_NORMAL)
            context.set_font_size(11.0)
            context.show_text(f'FPS ({self.backend}): {self.fps:.1f}')
    

    def on_timer_update_result(self):
        """Timer callback for textoverlay.

        :return: True to ensure the timer continues
        """
        if self.running:
            if self.current_label_index != self.new_label_index:
                # update textoverlay
                self.current_label_index = self.new_label_index
                label = self.tflite_get_label(self.current_label_index)
                textoverlay = self.pipeline.get_by_name('tensor_res')
                textoverlay.set_property('text', label)
        return True

    def set_window_title(self, name, title):
        """Set window title.

        :param name: GstXImageSink element name
        :param title: window title
        :return: None
        """
        element = self.pipeline.get_by_name(name)
        if element is not None:
            pad = element.get_static_pad('sink')
            if pad is not None:
                tags = Gst.TagList.new_empty()
                tags.add_value(Gst.TagMergeMode.APPEND, 'title', title)
                pad.send_event(Gst.Event.new_tag(tags))


    def sigint_handler(self, signal, frame):
        print("handling interrupt.")
        self.pipeline.set_state(Gst.State.NULL)
        self.mainloop.quit()

    def timeout_function(self):
        """GLib timer callback function implementation.
        """
        restart = True
        c = sys.stdin.read(1)
        if len(c) > 0:
            if c == '\x1b':  # escape
                self.mainloop.quit()
                restart = False

        return restart
    




if __name__ == '__main__':
    example = NNStreamerExample(sys.argv[1:])
    example.run_example()
