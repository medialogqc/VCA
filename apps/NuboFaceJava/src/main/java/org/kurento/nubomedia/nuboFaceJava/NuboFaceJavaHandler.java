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
package org.kurento.nubomedia.nuboFaceJava;

import java.io.IOException;
import java.util.concurrent.ConcurrentHashMap;

import org.kurento.client.EventListener;
import org.kurento.client.IceCandidate;
import org.kurento.client.KurentoClient;
import org.kurento.client.MediaPipeline;
import org.kurento.client.OnIceCandidateEvent;
import org.kurento.client.WebRtcEndpoint;
import org.kurento.jsonrpc.JsonUtils;
import org.kurento.module.nubofacedetector.*;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.web.socket.TextMessage;
import org.springframework.web.socket.WebSocketSession;
import org.springframework.web.socket.handler.TextWebSocketHandler;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonObject;

/**
 * Chroma handler (application and media logic).
 * 
 * @author Victor Manuel Hidalgo (vmhidalgo@visual-tools.com)
 * @since 6.0.0
 */
public class NuboFaceJavaHandler extends TextWebSocketHandler {

    private final Logger log = LoggerFactory.getLogger(NuboFaceJavaHandler.class);
    private static final Gson gson = new GsonBuilder().create();

    private final ConcurrentHashMap<String, UserSession> users = new ConcurrentHashMap<String, UserSession>();

    @Autowired
    private KurentoClient kurento;

    private NuboFaceDetector face = null;
    private int visualizeFace = -1;

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
	    setVisualization(session,jsonMessage);
	    break;	
	case "scale_factor":
	    log.debug("Case scale factor");
	    setScaleFactor(session,jsonMessage);
	    break;
	case "process_num_frames":
	    log.debug("Case process num frames");
	    setProcessNumberFrames(session,jsonMessage);
	    break;
	case "width_to_process":
	    log.debug("Case width to process");
	    setWidthToProcess(session,jsonMessage);
	    break;
	case "stop": {
	    UserSession user = users.remove(session.getId());
	    if (user != null) {
		user.release();
	    }
	    break;
	}
	case "onIceCandidate": {
	    JsonObject candidate = jsonMessage.get("candidate")
		.getAsJsonObject();

	    UserSession user = users.get(session.getId());
	    if (user != null) {
		IceCandidate cand = new IceCandidate(candidate.get("candidate")
						     .getAsString(), candidate.get("sdpMid").getAsString(),
						     candidate.get("sdpMLineIndex").getAsInt());
		user.addCandidate(cand);
	    }
	    break;
	}

	default:
	    sendError(session,
		      "Invalid message with id "
		      + jsonMessage.get("id").getAsString());
	    break;
	}
    }

    private void start(final WebSocketSession session, JsonObject jsonMessage) {
	try {
	    // Media Logic (Media Pipeline and Elements)
	    UserSession user = new UserSession();
	    MediaPipeline pipeline = kurento.createMediaPipeline();
	    user.setMediaPipeline(pipeline);
	    WebRtcEndpoint webRtcEndpoint = new WebRtcEndpoint.Builder(pipeline)
		.build();
	    user.setWebRtcEndpoint(webRtcEndpoint);
	    users.put(session.getId(), user);

	    webRtcEndpoint
		.addOnIceCandidateListener(new EventListener<OnIceCandidateEvent>() {

			@Override
			    public void onEvent(OnIceCandidateEvent event) {
			    JsonObject response = new JsonObject();
			    response.addProperty("id", "iceCandidate");
			    response.add("candidate", JsonUtils
					 .toJsonObject(event.getCandidate()));
			    try {
				synchronized (session) {
				    session.sendMessage(new TextMessage(
									response.toString()));
				}
			    } catch (IOException e) {
				log.debug(e.getMessage());
			    }
			}
		    });

	    face = new NuboFaceDetector.Builder(pipeline).build();

			
	    webRtcEndpoint.connect(face);
	    face.connect(webRtcEndpoint);

	    // SDP negotiation (offer and answer)
	    String sdpOffer = jsonMessage.get("sdpOffer").getAsString();
	    String sdpAnswer = webRtcEndpoint.processOffer(sdpOffer);

	    // Sending response back to client
	    JsonObject response = new JsonObject();
	    response.addProperty("id", "startResponse");
	    response.addProperty("sdpAnswer", sdpAnswer);

	    synchronized (session) {
		session.sendMessage(new TextMessage(response.toString()));
	    }
	    webRtcEndpoint.gatherCandidates();

	} catch (Throwable t) {
	    sendError(session, t.getMessage());
	}
    }

    private void setVisualization(WebSocketSession session,JsonObject jsonObject)
    {

	try{
	    visualizeFace = jsonObject.get("val").getAsInt();
	    if (null != face)
		face.showFaces(visualizeFace);

	} catch (Throwable t){
	    sendError(session,t.getMessage());
	}
    }

    private void setScaleFactor(WebSocketSession session,JsonObject jsonObject)
    {
	
	try{
	    int scale = jsonObject.get("val").getAsInt();
	    
	    if (null != face)
		{
		    log.debug("Sending setscaleFactor...." + scale);
		    face.multiScaleFactor(scale);
		}
	    
	} catch (Throwable t){
	    sendError(session,t.getMessage());
	}
    }

    private void setProcessNumberFrames(WebSocketSession session,JsonObject jsonObject)
    {
	
	try{
	    int num_img = jsonObject.get("val").getAsInt();
	    
	    if (null != face)
		{
		    log.debug("Sending process num frames...." + num_img);
		    face.processXevery4Frames(num_img);
 		}
	    
	} catch (Throwable t){
	    sendError(session,t.getMessage());
	}
    }
		
    private void setWidthToProcess(WebSocketSession session,JsonObject jsonObject)
    {
	
	try{
	    int width = jsonObject.get("val").getAsInt();
	    
	    if (null != face)
		{
		    log.debug("Sending width...." + width);
		    face.widthToProcess(width);
		}
	    
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
