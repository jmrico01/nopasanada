"use strict";

function OnAspectChanged(narrow)
{
    if (narrow) {
        cssNarrow_.href = "../../../css/article-narrow.css";
    }
    else {
        cssNarrow_.href = "";
    }
}

function OnResize()
{
    // Called from resize.js
}

window.onload = function() {
    if (YOUTUBE_VIDEO_ID !== null) {
        try {
            let player = new YT.Player("articleVideo", {
                height: "100%",
                width: "100%",
                videoId: YOUTUBE_VIDEO_ID, // must be declared in the HTML
                playerVars: {
                    modestbranding: 1,
                    rel: 0
                },
                events: {
                    "onReady": function() {
                    },
                    "onStateChange": function(event) {
                        let state = event.data;
                        if (state === YT.PlayerState.PAUSED) {
                        }
                        else if (state === YT.PlayerState.PLAYING) {
                        }
                        else if (state === YT.PlayerState.ENDED) {
                        }
                    }
                }
            });
        }
        catch (e) {
            console.log(e);
        }
    }
};

$(document).ready(function() {
    $("#content").css("visibility", "visible");
});
