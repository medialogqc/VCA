
package org.kurento.nubomedia;

import java.io.IOException;




import java.util.concurrent.ConcurrentHashMap;

import org.kurento.client.MediaPipeline;
import org.kurento.client.WebRtcEndpoint;
import org.kurento.client.factory.KurentoClient;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.web.socket.TextMessage;
import org.springframework.web.socket.WebSocketSession;
import org.springframework.web.socket.handler.TextWebSocketHandler;
import org.kurento.module.nubotracker.*;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonArray;
import com.google.gson.JsonObject;

/**
 * Magic Mirror handler (application and media logic).
 * 
 * @author Boni Garcia (bgarcia@gsyc.es)
 * @since 5.0.0
 */

public class NuboTrackerJavaHandler extends TextWebSocketHandler {

	private final Logger log = LoggerFactory
			.getLogger(NuboTrackerJavaHandler.class);
	private static final Gson gson = new GsonBuilder().create();

	private ConcurrentHashMap<String, MediaPipeline> pipelines = new ConcurrentHashMap<String, MediaPipeline>();

	@Autowired
	private KurentoClient kurento;
	
	private NuboTracker tracker =null;
	

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

		case "stop":
			String sessionId = session.getId();
			if (pipelines.containsKey(sessionId)) {
				pipelines.get(sessionId).release();
				pipelines.remove(sessionId);
			}
			break;
		case "threshold":		
			
			setThreshold(session,jsonMessage);			
			break;
			
		case "min_area":
			setMinArea(session, jsonMessage);
			break;
			
		case "max_area":			
			setMaxArea(session, jsonMessage);			
			break;
		
		case "distance":				
			setDistance(session, jsonMessage);			
			break;
			
		case "visual_mode":
			setVisualMode(session,jsonMessage);
			break;
		default:
			sendError(session,
					"Invalid message with id "
							+ jsonMessage.get("id").getAsString());
			break;
		}
	}

	private void setThreshold(WebSocketSession session, JsonObject jsonObject) {
					
		int threshold;
		try{
			
			threshold = jsonObject.get("val").getAsInt();
			
			if (null != tracker)
				tracker.setThreshold(threshold);
			
		}catch (Throwable t)
		{
			System.out.println("Threshold ERROR");
			sendError(session, t.getMessage());
		}
	}
	
	private void setMinArea(WebSocketSession session, JsonObject jsonObject) {
		
		try{			
			int min_area= jsonObject.get("val").getAsInt();			
			if (null != tracker)
			tracker.setMinArea(min_area);
			
		}catch (Throwable t)
		{
			System.out.println("Error......");
			sendError(session, t.getMessage());
		}
	}
	
	private void setMaxArea(WebSocketSession session, JsonObject jsonObject) {		
		try{			
			float max_area= jsonObject.get("val").getAsFloat();			
			if (null != tracker)
			tracker.setMaxArea(max_area);			
		}catch (Throwable t)
		{			
			sendError(session, t.getMessage());
		}
	}
	
	private void setDistance(WebSocketSession session, JsonObject jsonObject) {
		try{
		
			int distance= jsonObject.get("val").getAsInt();
			
			if (null != tracker)
				tracker.setDistance(distance);
			
		}catch (Throwable t)
		{			
			sendError(session, t.getMessage());
		}		
	}
	private void setVisualMode(WebSocketSession session, JsonObject jsonObject) {
		try{
			
			int mode= jsonObject.get("val").getAsInt();
			
			if (null != tracker)
				tracker.setVisualMode(mode);				
				
		}catch (Throwable t)
		{			
			sendError(session, t.getMessage());
		}
	}
	
	private void start(WebSocketSession session, JsonObject jsonMessage) {
		try {
			// Media Logic (Media Pipeline and Elements)			
			MediaPipeline pipeline = kurento.createMediaPipeline();
			
			pipelines.put(session.getId(), pipeline);			
			WebRtcEndpoint webRtcEndpoint = new WebRtcEndpoint.Builder(pipeline)
					.build();
			
			tracker = new NuboTracker.Builder(pipeline).build();
			//setting up the face detector. 
			//for the moment the filter will not do anything. 					
			webRtcEndpoint.connect(tracker);			
			tracker.connect(webRtcEndpoint);
			
			// SDP negotiation (offer and answer)
			String sdpOffer = jsonMessage.get("sdpOffer").getAsString();
			String sdpAnswer = webRtcEndpoint.processOffer(sdpOffer);

			// Sending response back to client
			JsonObject response = new JsonObject();
			response.addProperty("id", "startResponse");
			response.addProperty("sdpAnswer",sdpAnswer);
			session.sendMessage(new TextMessage(response.toString()));
		} catch (Throwable t) {
			sendError(session, t.getMessage());
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
