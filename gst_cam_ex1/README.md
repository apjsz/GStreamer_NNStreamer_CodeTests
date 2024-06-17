Ensayo de pipeline que decodifica el flujo de rtsp enviado por la cámara de vigilancia.
Divide a través de una tee los dos flujos y representa en la salida normal los dos videos con resolucuines distintas.

Ensayo de los flujos. 

Partí de separar el flujo de una webcam que ingresa por v4l2src.


	std::string pipeStr =  "v4l2src ! videoconvert ! tee name=t "
			" t. ! queue leaky=2 max-size-buffers=10 ! "
			" videoscale ! video/x-raw,width=640,height=480,framerate=30/1 ! autovideosink "
			" t. ! queue leaky=2 max-size-buffers=10 ! "
			" videoscale ! video/x-raw,width=1280,height=720,framerate=30/1 ! autovideosink";
	


Para utilizar como fuente el flujo rtsp se creó el siguiente pipe:


	std::string pipeStr = "rtspsrc location=rtsp://video:<password@ip_address:port>/cam/realmonitor"
			"?channel=1&subtype=0&unicast=true&proto=Onvif latency=100 "
	  		" ! rtph265depay ! h265parse ! avdec_h265 ! videoscale "
	  		" ! videoconvert ! capsfilter caps=video/x-raw,width=1920,height=1080 ! autovideosink ";
	  		

Hay que tener en cuenta que si se usa en línea de comando desde gst-launch-1.0 hacen falta las comillas del uri "rtsp://video ... &proto=Onvif"
Si se  usa con get_parse_launch no hacen falta las comillas.


Separaré el flujo en dos de tamaños distintos. Hay que ver si es posible seleccionar el tipo de algoritmo de interpolación que se usa al cambiara el tamaño de los flujos.


	std::string pipeStr = "rtspsrc location=rtsp://video:<password@ip_address:port>/cam/realmonitor"
			"?channel=1&subtype=0&unicast=true&proto=Onvif latency=100 "
			" ! rtph265depay ! h265parse ! avdec_h265 ! videoconvert ! tee name=t "
			" t. ! queue leaky=2 max-size-buffers=10 "
			" ! videoscale ! capsfilter caps=video/x-raw,width=640,height=480 ! autovideosink "
			" t. ! queue leaky=2 max-size-buffers=10 "
			" ! videoscale ! capsfilter caps=video/x-raw,width=320,height=200 ! autovideosink ";
			
			
Separo el flujo en: source, 


/******************************************************************************************

Para calibrar la cámara con parámetros previamente calculados este esquema de pipeline paraece que funciona.

gst-launch-1.0 -vv v4l2src ! videoconvert ! cameraundistort settings="$DATA" ! videoconvert !

Con el archivo de DATA definido como:



<?xml version=\"1.0\"?>
	<opencv_storage>
		<cameraMatrix type_id=\"opencv-matrix\">
			<rows>3</rows>
			<cols>3</cols>
			<dt>f</dt>
			<data>
				2.85762378e+03 0. 1.93922961e+03
				0. 2.84566113e+03 1.12195850e+03
				0. 0. 1.
			</data>
		</cameraMatrix>
		<distCoeffs type_id=\"opencv-matrix\">
			<rows>5</rows>
			<cols>1</cols>
			<dt>f</dt>
			<data>
				-6.14039421e-01
				 4.00045455e-01
				 1.47132971e-03
				 2.46772077e-04
				-1.20407566e-01
			</data>
		</distCoeffs>
	</opencv_storage>
	
/*********************************************************************************************
