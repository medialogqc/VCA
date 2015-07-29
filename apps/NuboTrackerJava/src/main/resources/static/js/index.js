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

var ws = new WebSocket('ws://' + location.host + '/nubotracker');
var videoInput;
var videoOutput;
var webRtcPeer;
var state = null;

const I_CAN_START = 0;
const I_CAN_STOP = 1;
const I_AM_STARTING = 2;
const I_CHANGE_CONF = 3;
const I_APPLY_CONF = 4;


window.onload = function() {
	console = new Console();
    	console.log("Page loaded ...");
	videoInput = document.getElementById('videoInput');
	videoOutput = document.getElementById('videoOutput');
	setState(I_CAN_START);
}

window.onbeforeunload = function() {
	ws.close();
}

ws.onmessage = function(message) {
	var parsedMessage = JSON.parse(message.data);
	console.info('Received message: ' + message.data);

	switch (parsedMessage.id) {
	case 'startResponse':
		startResponse(parsedMessage);
		break;
	
	case 'iceCandidate':
	    webRtcPeer.addIceCandidate(parsedMessage.candidate, function (error) {
        if (!error) return;
	      console.error("Error adding candidate: " + error);
	    });
	    break;

	case 'error':
		if (state == I_AM_STARTING) {
			setState(I_CAN_START);
		}
		onError("Error message from server: " + parsedMessage.message);
		break;
	default:
		if (state == I_AM_STARTING) {
			setState(I_CAN_START);
		}
		onError('Unrecognized message', parsedMessage);
	}
}

function start() {
	console.log("Starting video call ...")
	// Disable start button
	setState(I_AM_STARTING);
	showSpinner(videoInput, videoOutput);

	console.log("Creating WebRtcPeer and generating local sdp offer ...");
        var options = {
	    localVideo: videoInput,
	    remoteVideo: videoOutput,
	    onicecandidate: onIceCandidate
	}

    webRtcPeer = new kurentoUtils.WebRtcPeer.WebRtcPeerSendrecv(options,
								function (error) {
								    if(error) {
									return console.error(error);
								    }
								    webRtcPeer.generateOffer (onOffer);
								});
}

function onOffer(error,offerSdp) {
    	if (error) return console.error ("Error generating the offer");
	console.info('Invoking SDP offer callback function ' + location.host);
	var message = {
		id : 'start',
		sdpOffer : offerSdp
	}
	sendMessage(message);
}

function onError(error) {
	console.error(error);
}


function onIceCandidate(candidate) {
	  console.log("Local candidate" + JSON.stringify(candidate));

	  var message = {
	    id: 'onIceCandidate',
	    candidate: candidate
	  };
	  sendMessage(message);
}

function startResponse(message) {
	setState(I_CAN_STOP);
	console.log("SDP answer received from server. Processing ...");
    	webRtcPeer.processAnswer (message.sdpAnswer, function (error) {
		if (error) return console.error (error);
	});
}

function stop() {
	console.log("Stopping video call ...");
	setState(I_CAN_START);
	if (webRtcPeer) {
		webRtcPeer.dispose();
		webRtcPeer = null;

		var message = {
			id : 'stop'
		}
		sendMessage(message);
	}
	hideSpinner(videoInput, videoOutput);
}

function setState(nextState) {
	switch (nextState) {
	case I_CAN_START:
	    	$('#start').attr('disabled', false);
		$('#stop').attr('disabled', true);
		$('#apply_conf').attr('disabled', true);
		break;

	case I_CAN_STOP:
	    $('#start').attr('disabled', true);
	    $('#stop').attr('disabled', false);
	    $('#apply_conf').attr('disabled', false);
		break;

	case I_AM_STARTING:
	    $('#start').attr('disabled', true);
	    $('#stop').attr('disabled', true);
	    $('#apply_conf').attr('disabled', true);	    
	    break;

	default:
		onError("Unknown state " + nextState);
		return;
	}
	state = nextState;
}

function sendMessage(message) {
	var jsonMessage = JSON.stringify(message);
	console.log('Senging message: ' + jsonMessage);
	ws.send(jsonMessage);
}

function showSpinner() {
	for (var i = 0; i < arguments.length; i++) {
		arguments[i].poster = './img/transparent-1px.png';
		arguments[i].style.background = "center transparent url('./img/spinner.gif') no-repeat";
	}
}

function hideSpinner() {
	for (var i = 0; i < arguments.length; i++) {
		arguments[i].src = '';
		arguments[i].poster = './img/webrtc.png';
		arguments[i].style.background = '';
	}
}


function show_faces() {
	var faces = 0;
	if ( document.getElementById('show_faces').checked) {
		faces=1;
	}
	
	var message = {
		id : 'show_faces',
		val : faces
	};
	
	sendMessage(message);
	
}

function showValue(newValue,val_id)
{
	var message = {
			id: val_id,
			val: newValue
	}
		
	document.getElementById(val_id).innerHTML=newValue;
	
	sendMessage(message);
	
}

function setVisualMode(mode)
{
	var message = {
			id: "visual_mode",
			val: mode
	}
	
	sendMessage(message);
}

/**
 * Lightbox utility (to display media pipeline image in a modal dialog)
 */
$(document).delegate('*[data-toggle="lightbox"]', 'click', function(event) {
	event.preventDefault();
	$(this).ekkoLightbox();
});
