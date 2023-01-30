function OnAspectChanged(narrow)
{
    if (narrow) {
        cssNarrow_.href = "/css/qrcode-narrow.css";
    }
    else {
        cssNarrow_.href = "";
    }
}

function OnResize()
{
}

$(document).ready(function() {
    OnResize();

    $("#content").css("visibility", "visible");
});
