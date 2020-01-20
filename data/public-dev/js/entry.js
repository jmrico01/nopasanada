"use strict";

let editors_ = null;

let imageRowTemplate_ = null;
let images_ = null;

let imageDropzone_ = null;

let uploadQueueInProcess_ = false;

function UpdateImageList(images)
{
    $.ajax({
        type: "GET",
        url: "/previewSite",
        contentType: "application/json",
        dataType: "json",
        async: true,
        data: "",
        success: function(data) {
            let previewUrl = data.url;
            let $imageList = $("#imagesList");
            $imageList.html("");

            for (let i = 0; i < images.length; i++) {
                let $imageRow = $(imageRowTemplate_);
                $imageRow.find(".rowImage img").attr("src", previewUrl + images[i].uri);
                $imageRow.find(".rowText h2").html(images[i].name);
                $imageList.append($imageRow);
            }

            images_ = images;
        },
        error: function(error) {
            console.error("UpdateImageList failed");
            console.error(error);
        }
    });
}

function GetEntryPath()
{
    let url = new URL(window.location.href);
    return url.searchParams.get("entry");
}

function OnContentTypeChanged()
{
    let selectedContentType = document.getElementById("contentType").value;

    $("*[class*=\"input-\"]").hide();
    $(".input-" + selectedContentType).show();
}

function LoadEntryData(entryData)
{
    let images = [];
    for (let key in entryData.media) {
        let mediaItem = entryData.media[key];
        if (mediaItem.$.type === "image") {
            images.push({
                name: key,
                uri: mediaItem._
            });
        }
    }

    let imagesDedup = [];
    for (let i = 0; i < images.length; i++) {
        let duplicate = false;
        for (let j = 0; j < imagesDedup.length; j++) {
            if (images[i].uri === imagesDedup[j].uri) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            imagesDedup.push(images[i]);
        }
    }
    UpdateImageList(imagesDedup);

    document.getElementById("contentType").value = entryData.contentType;
    OnContentTypeChanged();

    document.getElementsByName("featuredPretitle")[0].value = entryData.featured.pretitle;
    document.getElementsByName("featuredTitle")[0].value = entryData.featured.title;
    document.getElementsByName("featuredText1")[0].value = entryData.featured.text1;
    document.getElementsByName("featuredText2")[0].value = entryData.featured.text2;
    document.getElementsByName("highlightColor")[0].value = entryData.featured.highlightColor;

    document.getElementsByName("tags")[0].value = entryData.tags.join(", ");

    document.getElementsByName("title")[0].value = entryData.title;
    let titlePoster = entryData.titlePoster;
    if (titlePoster === undefined) {
        titlePoster = "";
    }
    document.getElementsByName("titlePoster")[0].value = titlePoster;
    document.getElementsByName("description")[0].value = entryData.description;
    let dateString = entryData.day + "/" + entryData.month + "/" + entryData.year;
    document.getElementsByName("date")[0].value = dateString;
    document.getElementsByName("color")[0].value = entryData.color;

    document.getElementsByName("author")[0].value = entryData.author;
    document.getElementsByName("youtubeID")[0].value = entryData.videoID;

    document.getElementsByName("customTop")[0].value = entryData.customTop;
    document.getElementsByName("title1")[0].value = entryData.title1;
    document.getElementsByName("author1")[0].value = entryData.author1;
    if ("text1" in entryData) {
        editors_["text1"].setContent(entryData.text1);
    }
    document.getElementsByName("title2")[0].value = entryData.title2;
    document.getElementsByName("author2")[0].value = entryData.author2;
    if ("text2" in entryData) {
        editors_["text2"].setContent(entryData.text2);
    }
    document.getElementsByName("title3")[0].value = entryData.title3;
    document.getElementsByName("author3")[0].value = entryData.author3;
    if ("text3" in entryData) {
        editors_["text3"].setContent(entryData.text3);
    }
    document.getElementsByName("title4")[0].value = entryData.title4;
    document.getElementsByName("author4")[0].value = entryData.author4;
    if ("text4" in entryData) {
        editors_["text4"].setContent(entryData.text4);
    }
    document.getElementsByName("subtitle")[0].value = entryData.subtitle;
    if ("text" in entryData) {
        editors_["text"].setContent(entryData.text);
    }
}

function GetImageByName(name, images)
{
    for (let i = 0; i < images.length; i++) {
        if (images[i].name === name) {
            return images[i];
        }
    }

    return null;
}

function SaveEntryData()
{
    let date = document.getElementsByName("date")[0].value;
    let dateSplit = date.split("/");
    for (let i = 0; i < dateSplit.length; i++) {
        dateSplit[i] = dateSplit[i].trim();
    }
    let dateError = null;
    if (dateSplit.length !== 3) {
        dateError = "expected 2 slashes";
    }
    if (dateSplit[0].length !== 2) {
        dateError = "bad day format";
    }
    if (dateSplit[1].length !== 2) {
        dateError = "bad month format";
    }
    let monthInt = parseInt(dateSplit[1]);
    if (monthInt <= 0 || monthInt > 12) {
        dateError = "month not 1 through 12";
    }
    if (dateSplit[2].length !== 4) {
        dateError = "bad year format";
    }
    if (dateError !== null) {
        alert("Incorrect date \"" + date + "\", should be dd/mm/yyyy (" + dateError + ")");
        return;
    }

    let tags = document.getElementsByName("tags")[0].value.split(",");
    for (let i = 0; i < tags.length; i++) {
        tags[i] = tags[i].trim();
    }

    if (GetImageByName("header", images_) === null) {
        alert("Missing header image");
        return;
    }
    let media = {};
    for (let i = 0; i < images_.length; i++) {
        let image = images_[i];
        media[image.name] = {
            _: image.uri,
            $: {
                type: "image"
            }
        };
    }

    let entryData = {
        contentType: document.getElementById("contentType").value,
        featured: {
            pretitle:       document.getElementsByName("featuredPretitle")[0].value,
            title:          document.getElementsByName("featuredTitle")[0].value,
            text1:          document.getElementsByName("featuredText1")[0].value,
            text2:          document.getElementsByName("featuredText2")[0].value,
            highlightColor: document.getElementsByName("highlightColor")[0].value
        },

        media:       media,

        tags:        tags,

        title:       document.getElementsByName("title")[0].value,
        titlePoster: document.getElementsByName("titlePoster")[0].value,
        description: document.getElementsByName("description")[0].value,
        day:         dateSplit[0],
        month:       dateSplit[1],
        year:        dateSplit[2],
        color:       document.getElementsByName("color")[0].value,

        author:      document.getElementsByName("author")[0].value,
        videoID:     document.getElementsByName("youtubeID")[0].value,
        customTop:   document.getElementsByName("customTop")[0].value,
        title1:      document.getElementsByName("title1")[0].value,
        author1:     document.getElementsByName("author1")[0].value,
        text1:       editors_["text1"].getContent(),
        title2:      document.getElementsByName("title2")[0].value,
        author2:     document.getElementsByName("author2")[0].value,
        text2:       editors_["text2"].getContent(),
        title3:      document.getElementsByName("title3")[0].value,
        author3:     document.getElementsByName("author3")[0].value,
        text3:       editors_["text3"].getContent(),
        title4:      document.getElementsByName("title4")[0].value,
        author4:     document.getElementsByName("author4")[0].value,
        text4:       editors_["text4"].getContent(),
        subtitle:    document.getElementsByName("subtitle")[0].value,
        text:        editors_["text"].getContent(),
    };

    for (let k in entryData) {
        if (entryData[k] === "undefined") {
            delete entryData[k];
        }
    }

    $("#statusMessage").html("Saving...");
    $.ajax({
        type: "POST",
        url: GetEntryPath(),
        contentType: "application/json",
        async: true,
        data: JSON.stringify(entryData),
        success: function(data) {
            $("#statusMessage").html("Entry save successful.");
        },
        error: function(xhr, textStatus, error) {
            $("#statusMessage").html("Entry save failed, error: " + textStatus);
        }
    });
}

function BuzzImageUploadQueue()
{
    let queued = imageDropzone_.getQueuedFiles();
    if (queued.length === 0) {
        return;
    }

    if (uploadQueueInProcess_) {
        return;
    }
    uploadQueueInProcess_ = true;

    let image = queued[0];
    $(image.previewElement).css("background-color", "#666");
    $("#imageUploadFileName").html(image.name);
    $("#imageUploadNameCustom").hide();
    $("#imageUploadProcessor").show();

    let typeHtml = "";
    typeHtml += "<option value=\"header\">Header</option>";
    typeHtml += "<option value=\"poster\">Poster</option>";
    let contentType = document.getElementById("contentType").value;
    if (contentType === "newsletter") {
        for (let i = 0; i < 4; i++) {
            typeHtml += "<option value=\"header-desktop" + (i + 1) + "\">Newsletter Desktop Header, Article " + (i + 1) + "</option>";
        }
        for (let i = 0; i < 4; i++) {
            typeHtml += "<option value=\"header-mobile" + (i + 1) + "\">Newsletter Mobile Header, Article " + (i + 1) + "</option>";
        }
    }
    typeHtml += "<option value=\"custom\">Custom</option>";
    $("#imageUploadType").html(typeHtml);

    $("#imageUploadType").off("change");
    $("#imageUploadType").change(function() {
        let uploadType = $("#imageUploadType").val();
        if (uploadType === "custom") {
            $("#imageUploadNameCustom").show();
        }
        else {
            $("#imageUploadNameCustom").hide();
        }
    });

    $("#imageUploadButton").off("click");
    $("#imageUploadButton").click(function() {
        let imageLabel = $("#imageUploadType").val();
        if (imageLabel === "custom") {
            imageLabel = $("input[name=imageUploadNameCustom]").val();
        }
        let uploadTypeRegex = /^[a-z0-9\-]+$/g;
        if (!uploadTypeRegex.test(imageLabel)) {
            $("#statusMessage").html("Image label should only have lower-case letters, numbers, or dashes.");
            return;
        }
        image.npnLabel = imageLabel;
        imageDropzone_.processFile(image);
        $("#imageUploadProcessor").hide();
        $(image.previewElement).css("background-color", "#fff");
        uploadQueueInProcess_ = false;
    });

    $("#imageUploadCancelButton").off("click");
    $("#imageUploadCancelButton").click(function() {
        imageDropzone_.removeFile(image);
        $(image.previewElement).css("background-color", "#fff");
        uploadQueueInProcess_ = false;
        BuzzImageUploadQueue();
    });
}

Dropzone.options.imageDropzone = {
    autoProcessQueue: false,
    paramName: "imageFile",
    acceptedFiles: "image/jpeg",
    dictDefaultMessage: "Click here to upload, or drag an image (JPG only)",
    init: function() {
        imageDropzone_ = this;

        this.on("sending", function(file, xhr, formData) {
            formData.set("npnEntryPath", GetEntryPath());
            formData.set("npnLabel", file.npnLabel);
        });
        this.on("success", function(file, response) {
            let parser = new DOMParser();
            let xmlDoc = parser.parseFromString(response, "text/xml");
            let imageUri = xmlDoc.getElementsByTagName("uri")[0].textContent;
            let image = GetImageByName(file.npnLabel, images_);
            if (image === null) {
                images_.push({
                    name: file.npnLabel,
                    uri: imageUri
                });
            }
            else {
                image.uri = imageUri;
            }
            console.log(images_);

            $("#statusMessage").html("Successfully uploaded " + file.name + " as " + file.npnLabel);
            BuzzImageUploadQueue();
            SaveEntryData();
        });
        this.on("complete", function(file) {
        });
    },
    accept: function(file, done) {
        if (file.type !== "image/jpeg") {
            done("Image format must be .jpg");
            return;
        }
        done();

        BuzzImageUploadQueue();
    }
};

$(document).ready(async function() {
    $(".section").hide();

    $("#imageUploadProcessor").hide();
    imageRowTemplate_ = $("#imagesListRowTemplate").html();
    $("#imagesListRowTemplate").remove();

    $.ajax({
        type: "GET",
        url: "/previewSite",
        contentType: "application/json",
        dataType: "json",
        async: true,
        data: "",
        success: function(data) {
            let previewUrl = data.url;
            $("#previewLink").attr("href", previewUrl + GetEntryPath());
        },
        error: function(error) {
            console.error(error);
        }
    });

    tinymce.init({
        selector: "tinymce",
        style_formats: [
            { title: "Paragraph", block: "p", style: {} }
        ],
        menubar: false,
        plugins: "autoresize colorpicker hr lists link paste",
        toolbar: "undo redo | bold italic | alignleft aligncenter " +
            "alignright alignjustify | link hr | bullist numlist | removeformat",
        paste_as_text: true,
        font_formats: "Default (Futura)=FuturaPTBook;",
        content_css: [
            "https://nopasanada.com/css/global.css",
            "https://nopasanada.com/css/article.css"
        ],
    }).then(function(editors) {
        editors_ = {};
        for (let i = 0; i < editors.length; i++) {
            editors_[editors[i].id] = editors[i];
        }

        $.ajax({
            type: "GET",
            url: GetEntryPath(),
            contentType: "application/json",
            dataType: "json",
            async: true,
            data: "",
            success: function(data) {
                LoadEntryData(data);

                $("#contentType").change(function() {
                    OnContentTypeChanged();
                });

                setInterval(function() {
                    SaveEntryData();
                }, 20000);
                $("#saveButton").click(function() {
                    SaveEntryData();
                });

                $("#deleteButton").click(function() {
                    let requestData = {
                        uri: GetEntryPath()
                    };
                    $.ajax({
                        type: "POST",
                        url: "/deleteEntry",
                        contentType: "application/json",
                        async: true,
                        data: JSON.stringify(requestData),
                        success: function(data) {
                            $("#statusMessage").html("Entry deleted successfully.");
                            window.location = "/";
                        },
                        error: function(error) {
                            console.error(error);
                            $("#statusMessage").html("Delete failed, error: " + error);
                        }
                    });
                });

                $(".section").show();
            },
            error: function(error) {
                console.error(error);
            }
        });
    });
});
