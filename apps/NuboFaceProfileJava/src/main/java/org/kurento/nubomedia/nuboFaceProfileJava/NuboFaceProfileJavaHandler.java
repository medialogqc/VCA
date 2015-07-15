/*
 * (C) Copyright 2014 Kurento (http://kurento.org/)
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 */
package org.kurento.nubomedia.nuboFaceProfileJava;

import java.io.IOException;
import java.util.concurrent.ConcurrentHashMap;
import org.kurento.client.MediaPipeline;
import org.kurento.client.WebRtcEndpoint;
import org.kurento.client.KurentoClient;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.web.socket.TextMessage;
import org.springframework.web.socket.WebSocketSession;
import org.springframework.web.socket.handler.TextWebSocketHandler;

import org.kurento.module.nubofacedetector.*;
import org.kurento.module.nubomouthdetector.*;
import org.kurento.module.nubonosedetector.*;
import org.kurento.module.nuboeyedetector.*;
import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonObject;

/**
 * Magic Mirror handler (application and media logic).
 * 
 * @author Victor Hidalgo (vmhidalgo@visual-tools.com)
 * @since 5.1.0
 */

public class NuboFaceProfileJavaHandler extends TextWebSocketHandler {

	private final Logger log = LoggerFactory
			.getLogger(NuboFaceProfileJavaHandler.class);
	private static final Gson gson = new GsonBuilder().create();

	private ConcurrentHashMap<String, MediaPipeline> pipelines = new ConcurrentHashMap<String, MediaPipeline>();

	@Autowired
	private KurentoClient kurento;
	
	private NuboFaceDetector face = null;
	private NuboMouthDetector mouth = null;
	private NuboNoseDetector nose = null;
	private NuboEyeDetector eye = null;
			
	private int visualizeFace  = -1;
	private int visualizeMouth = -1;
	private int visualizeNose  = -1;
	private int visualizeEye   = -1;
	@Override
	public void handleTextMessage(WebSocketSession session, TextMessage message)
			throws Exception {
		JsonObject jsonMessage = gson.fromJson(message.getPayload(),
				JsonObject.class);

		log.debug("Incoming message: {}", jsonMessage);

		switch (jsonMessage.get("id").getAsString()) {
		case "start":
			start(session, jsonMessage);
			break;
		case "show_faces":
			setViewFaces(session,jsonMessage);
			break;
			
		case "show_mouths":
			setViewMouths(session,jsonMessage);
			break;
			
		case "show_noses":
			setViewNoses(session,jsonMessage);
			break;
		
		case "show_eyes":
			setViewEyes(session,jsonMessage);
			break;
			
		case "stop":
			String sessionId = session.getId();
			if (pipelines.containsKey(sessionId)) {
				pipelines.get(sessionId).release();
				pipelines.remove(sessionId);
			}
			break;

		default:
			sendError(session,
					"Invalid message with id "
							+ jsonMessage.get("id").getAsString());
			break;
		}
	}

	private void start(WebSocketSession session, JsonObject jsonMessage) {
		try {
			// Media Logic (Media Pipeline and Elements)
			MediaPipeline pipeline = kurento.createMediaPipeline();
			pipelines.put(session.getId(), pipeline);

			WebRtcEndpoint webRtcEndpoint = new WebRtcEndpoint.Builder(pipeline)
					.build();
			
			face = new NuboFaceDetector.Builder(pipeline).build();							
			face.sendMetaData(1);
			face.detectByEvent(0);
			face.showFaces(0);

			mouth = new NuboMouthDetector.Builder(pipeline).build();
			mouth.sendMetaData(1);
			mouth.detectByEvent(1);
			mouth.showMouths(0);
			
			nose = new NuboNoseDetector.Builder(pipeline).build();
			nose.sendMetaData(1);
			nose.detectByEvent(1);
			nose.showNoses(0);
			
			eye = new NuboEyeDetector.Builder(pipeline).build();
			eye.sendMetaData(0);
			eye.detectByEvent(1);
			eye.showEyes(0);
			
			webRtcEndpoint.connect(face);
			face.connect(mouth);
			mouth.connect(nose);
			nose.connect(eye);
			eye.connect(webRtcEndpoint);
			
			
			// SDP negotiation (offer and answer)
			String sdpOffer = jsonMessage.get("sdpOffer").getAsString();
			String sdpAnswer = webRtcEndpoint.processOffer(sdpOffer);

			// Sending response back to client
			JsonObject response = new JsonObject();
			response.addProperty("id", "startResponse");
			response.addProperty("sdpAnswer", sdpAnswer);
			session.sendMessage(new TextMessage(response.toString()));
		} catch (Throwable t) {
			sendError(session, t.getMessage());
		}
	}
	
	private void setViewFaces(WebSocketSession session,JsonObject jsonObject)
	{
		
		try{
			visualizeFace = jsonObject.get("val").getAsInt();
				
			if (null != face)
				face.showFaces(visualizeFace);
			
		} catch (Throwable t){
			sendError(session,t.getMessage());
		}
	}
	
	private void setViewMouths(WebSocketSession session,JsonObject jsonObject)
	{
		
		try{
			visualizeMouth = jsonObject.get("val").getAsInt();
				
			if (null != mouth)
				mouth.showMouths(visualizeMouth);
			
		} catch (Throwable t){
			sendError(session,t.getMessage());
		}
	}
	
	private void setViewNoses(WebSocketSession session,JsonObject jsonObject)
	{
		
		try{
			visualizeNose = jsonObject.get("val").getAsInt();
				
			if (null != nose)
				nose.showNoses(visualizeNose);
			
		} catch (Throwable t){
			sendError(session,t.getMessage());
		}
	}

	private void setViewEyes(WebSocketSession session,JsonObject jsonObject)
	{
		
		try{
			visualizeEye = jsonObject.get("val").getAsInt();
				
			if (null != eye)
				eye.showEyes(visualizeEye);
			
		} catch (Throwable t){
			sendError(session,t.getMessage());
		}
	}
	
	private void sendError(WebSocketSession session, String message) {
		try {
			JsonObject response = new JsonObject();
			response.addProperty("id", "error");
			response.addProperty("message", message);
			session.sendMessage(new TextMessage(response.toString()));
		} catch (IOException e) {
			log.error("Exception sending message", e);
		}
	}
}
