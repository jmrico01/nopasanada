"use strict";

// const IMAGE_BASE_URL = "https://nopasanada.s3.amazonaws.com";
const IMAGE_BASE_URL = ".";

const FEATURED_IMAGE_FADE_MS = 400;
const IMAGE_ANIM_MS = 250;

const TAG_HOME       = "home";
const TAG_COLLECTION = "cultura";
const TAG_ARTICLE    = "cultura";
const TAG_VIDEO      = "cultura";

let allEntries_ = null;
let featuredEntries_ = null;
let loadedEntries_ = null; // TODO revisit name + use-case, will need more groups of stuff
let collectionEntries_ = null;
let recentArticleEntries_ = null;
let recentVideosEntries_ = null;

let featuredImages_ = null;
let collectionImages_ = null;
let recentArticleImages_ = null;
let recentVideoImages_ = null;

let collectionTemplate_ = null;
let recentArticleTemplate_ = null;
let recentVideoTemplate_ = null;
let postersPerScreen_ = 5;
let posterPositionIndex_ = 0;

let prevCategory_ = null;
let imgCycleInterval_ = null;

function GetCurrentCategory()
{
    if (featuredEntries_ !== null) {
        let hash = window.location.hash;
        let hashIndex = hash.indexOf("#");
        if (hashIndex !== -1) {
            let hashCategory = hash.substring(hashIndex + 1, hash.length);
            if (featuredEntries_.hasOwnProperty(hashCategory) !== -1) {
                return hashCategory;
            }
        }
    }

    return TAG_HOME;
}

function SetFeaturedInfo(entry)
{
    $("#featuredPretitle").html(entry.pretitle);
    $("#featuredTitle a").html(entry.title);
    $("#featuredTitle a").attr("href", entry.uri);
    $("#featuredText1").html(entry.text1);
    $("#featuredText2").html(entry.text2);
    $("#header a").unbind("mouseover mouseout");
    $("#header a").mouseover(function() {
        $(this).css("color", entry.highlightColor)
    });
    $("#header a").mouseout(function() {
        $(this).css("color", "#ffffff");
    });
}

function MovePosters(entries, indexDelta)
{
    let indexMax = Math.floor((entries.length - 1) / postersPerScreen_);
    posterPositionIndex_ = Math.min(Math.max(posterPositionIndex_ + indexDelta, 0), indexMax);

    $("#contentList").css("margin-left", -posterPositionIndex_ * window.innerWidth);
    if (posterPositionIndex_ == 0) {
        $("#contentArrowLeftButton").hide();
    }
    else {
        $("#contentArrowLeftButton").show();
    }
    if (posterPositionIndex_ == indexMax) {
        $("#contentArrowRightButton").hide();
    }
    else {
        $("#contentArrowRightButton").show();
    }
}

function SetPosterContentWidth(entries)
{
    let width = Math.ceil(entries.length / postersPerScreen_) * window.innerWidth;
    $("#contentList").css("width", width);
}

function ResetPosters(entries)
{
    return;
    SetPosterContentWidth(entries);
    let $contentList = $("#contentList");
    $contentList.html("");

    $contentList.append("<div class=\"entrySpaceEdge\"></div>");
    for (let i = 0; i < entries.length; i++) {
        let entryData = entries[i];

        let $entry = $(posterTemplate_);
        $entry.find("a").attr("href", entryData.uri);
        $entry.find("img").attr("src", IMAGE_BASE_URL + entryData.image);
        $entry.find(".entryNumber").html(i + 1 + ".");
        $entry.find(".entryText").html(entryData.title);
        $contentList.append($entry);

        if (i !== entries.length - 1) {
            if ((i + 1) % postersPerScreen_ === 0) {
                $contentList.append("<div class=\"entrySpaceEdge\"></div>");
                if (isNarrow_) {
                    $contentList.append("<div style=\"width: 100%; height: 65vw;\"></div>");
                }
                $contentList.append("<div class=\"entrySpaceEdge\"></div>");
            }
            else {
                $contentList.append("<div class=\"entrySpace\"></div>");
            }
        }
    }

    posterPositionIndex_ = 0;
    MovePosters(entries, 0);
}

function ResetCollectionEntries(entries)
{
    let $list = $("#collectionList");
    $list.html("");

    for (let i = 0; i < entries.length; i++) {
        let entryData = entries[i];
        let $entry = $(collectionTemplate_);
        $entry.find("a").addBack("a").attr("href", entryData.uri);
        $entry.find("img").attr("src", IMAGE_BASE_URL + entryData.image);
        $list.append($entry);
    }
}

function ResetRecentArticleEntries(entries)
{
    let $list = $("#recentArticleList");
    $list.html("");

    for (let i = 0; i < entries.length; i++) {
        let entryData = entries[i];
        let $entry = $(recentArticleTemplate_);
        $entry.find("a").addBack("a").attr("href", entryData.uri);
        $entry.find("img").attr("src", IMAGE_BASE_URL + entryData.image);
        $entry.find(".recentArticleTitle").html(entryData.title.toUpperCase());
        let metadataString = "";
        if (entryData.hasOwnProperty("author") && entryData.author.length > 0) {
            metadataString += "POR " + entryData.author.toUpperCase() + " | ";
        }
        const months = [
            "ENERO", "FEBRERO", "MARZO",
            "ABRIL", "MAYO", "JUNIO",
            "JULIO", "AGOSTO", "SEPTIEMBRE",
            "OCTUBRE", "NOVIEMBRE", "DICIEMBRE"
        ];
        let monthInd = parseInt(entryData.dateString.substring(4, 6)) - 1;
        metadataString += entryData.dateString.substring(6, 8) + " DE " + months[monthInd];
        $entry.find(".recentArticleMetadata").html(metadataString);
        $entry.find(".recentArticleSubtitle").html(entryData.subtitle);
        $list.append($entry);
    }
}

function ResetRecentVideoEntries(entries)
{
    let $list = $("#recentVideoList");
    $list.html("");

    for (let i = 0; i < entries.length; i++) {
        let entryData = entries[i];
        let $entry = $(recentVideoTemplate_);
        $entry.find("a").addBack("a").attr("href", entryData.uri);
        $entry.find("img").attr("src", IMAGE_BASE_URL + entryData.image);
        $entry.find(".recentVideoTitle").html(entryData.title.toUpperCase());
        $list.append($entry);
    }
}

function OnAspectChanged(narrow)
{
    if (narrow) {
        cssNarrow_.href = "css/main-narrow.css";
        postersPerScreen_ = 3;
        $("#contentList").css("width", "100%");
    }
    else {
        cssNarrow_.href = "";
        postersPerScreen_ = 5;
    }

    if (loadedEntries_ !== null) {
        ResetPosters(loadedEntries_);
    }
}

function OnResize()
{
    if (isNarrow_) {
        $("#coleccion").css("height", "auto");
    }

    if (loadedEntries_ !== null) {
        SetPosterContentWidth(loadedEntries_);
    }
}

function HandleScroll()
{
    let headerOpacity = Math.min(document.documentElement.scrollTop / window.innerHeight, 1.0);
    $("#header").css("background-color", "rgba(0%, 0%, 0%, " + headerOpacity * 100.0 + "%)");
}

window.onscroll = HandleScroll;

function IsFeaturedImageSetLoaded(imageSet)
{
    if (imageSet.length === 0) {
        console.error("imageSet of length 0 for " + category + ", " + featuredIndex);
        return false;
    }
    for (let i = 0; i < imageSet.length; i++) {
        if (!imageSet[i].hasOwnProperty("loaded") || !imageSet[i].loaded) {
            return false;
        }
    }
    return true;
}

function SetFeaturedImageSet(imageSet)
{
    if (imgCycleInterval_ !== null) {
        clearInterval(imgCycleInterval_);
        imgCycleInterval_ = null;
    }

    let $currentActive = $("#landingImageCycler img.active");
    let $currentTransition = $("#landingImageCycler img.transition");
    let $nextActive = $("#" + imageSet[0].divId);
    if ($currentActive.length === 0 && $currentTransition.length === 0) {
        $nextActive.addClass("active");
    }
    else if ($currentActive.length > 0 && $currentTransition.length === 0) {
        $nextActive.addClass("transition");
        $currentActive.fadeOut(FEATURED_IMAGE_FADE_MS, function() {
            $currentActive.removeClass("active").show();
            $(".transition").addClass("active").removeClass("transition");
        });
    }
    else if ($currentActive.length === 0 && $currentTransition.length > 0) {
        // strange case, small timing issue, but this is JS so who knows
    }
    else if ($currentActive.length > 0 && $currentTransition.length > 0) {
        $(".transition").removeClass("transition");
        $nextActive.addClass("transition");
    }

    let counter = 0;
    let counterDir = 1;
    imgCycleInterval_ = setInterval(function() {
        let numImages = imageSet.length;
        if (numImages === 1) {
            return;
        }
        let imageId = imageSet[counter].divId;
        let $currentActive = $("#landingImageCycler img.active");
        $currentActive.removeClass("active");
        let $featuredImage = $("#" + imageId).addClass("active");
        if (counter >= numImages - 1) {
            counterDir = -1;
        }
        else if (counter <= 0) {
            counterDir = 1;
        }
        counter += counterDir;
    }, IMAGE_ANIM_MS);
}

function OnFeaturedImageSetLoaded(imageSet)
{
    for (let i = 0; i < imageSet.length; i++) {
        $("#" + imageSet[i].divId).attr("src", imageSet[i].src);
    }
}

function LoadFeaturedImageSetIfNotLoaded(imageSet, callback)
{
    if (IsFeaturedImageSetLoaded(imageSet)) {
        callback();
        return;
    }
    for (let i = 0; i < imageSet.length; i++) {
        if (imageSet[i].loading) {
            continue;
        }
        imageSet[i].loading = true;
        imageSet[i].onload = function() {
            imageSet[i].loaded = true;
            imageSet[i].loading = false;
            if (IsFeaturedImageSetLoaded(imageSet)) {
                OnFeaturedImageSetLoaded(imageSet);
                callback();
            }
        };
        imageSet[i].src = IMAGE_BASE_URL + imageSet[i].imageUrl;
    }
}

function LoadAllFeaturedImageSets()
{
    for (let category in featuredImages_) {
        for (let i = 0; i < featuredImages_[category].length; i++) {
            let imageSet = featuredImages_[category][i];
            if (IsFeaturedImageSetLoaded(imageSet)) {
                continue;
            }
            LoadFeaturedImageSetIfNotLoaded(imageSet, function() {
                // pretty dumb, sure... but whatever
                LoadAllFeaturedImageSets();
            });
        }
    }
}

function OnFeaturedEntriesLoaded(featured)
{
    // Fill global featured entries
    featuredEntries_ = {};
    for (let category in featured) {
        featuredEntries_[category] = [];
        for (let i = 0; i < featured[category].length; i++) {
            const uri = featured[category][i];
            for (let j = 0; j < allEntries_.length; j++) {
                if (allEntries_[j].uri === uri) {
                    let entry = allEntries_[j].featuredInfo;
                    entry.uri = uri;
                    featuredEntries_[category].push(entry);
                    break;
                }
            }
        }
    }

    // Fill global featured images, link up to HTML
    featuredImages_ = {};
    for (let category in featuredEntries_) {
        featuredImages_[category] = [];
        for (let i = 0; i < featuredEntries_[category].length; i++) {
            const entry = featuredEntries_[category][i];
            let images = [];
            for (let j = 0; j < entry.images.length; j++) {
                let img = new Image;
                img.loaded = false;
                img.loading = false;
                img.divId = "featuredInfo-" + category + "-" + featuredImages_[category].length.toString() + "-" + j.toString();
                img.imageUrl = entry.images[j];
                images.push(img);
                $("#landingImageCycler").append("<img id=\"" + img.divId + "\" class=\"featuredImage\" src=\"\">");
            }
            featuredImages_[category].push(images);
        }
    }

    $(".featuredImage").hide();

    let startCategory = GetCurrentCategory();
    SetFeaturedInfo(featuredEntries_[startCategory][0]);
    let startImageSet = featuredImages_[startCategory][0];
    LoadFeaturedImageSetIfNotLoaded(startImageSet, function() {
        SetFeaturedImageSet(startImageSet);
        $(".featuredImage").show();
        LoadAllFeaturedImageSets();
    });
}

function OnAllEntriesLoaded(entries)
{
    allEntries_ = entries;
    collectionEntries_ = [];
    recentArticleEntries_ = [];
    recentVideosEntries_ = [];
    for (let i = 0; i < allEntries_.length; i++) {
        const entry = allEntries_[i];
        const isHome = entry.tags.indexOf(TAG_HOME) !== -1;
        if (isHome) {
            if (entry.tags.indexOf(TAG_COLLECTION) !== -1) {
                collectionEntries_.push(JSON.parse(JSON.stringify(entry)));
            }
            if (entry.tags.indexOf(TAG_ARTICLE) !== -1) {
                recentArticleEntries_.push(JSON.parse(JSON.stringify(entry)));
            }
            if (entry.tags.indexOf(TAG_VIDEO) !== -1) {
                recentVideosEntries_.push(JSON.parse(JSON.stringify(entry)));
            }
        }
    }

    ResetCollectionEntries(collectionEntries_);
    ResetRecentArticleEntries(recentArticleEntries_);
    ResetRecentVideoEntries(recentVideosEntries_);
}

function OnHashChanged()
{
    let category = GetCurrentCategory();
    if (category !== prevCategory_) {
        prevCategory_ = category;
        SetFeaturedInfo(featuredEntries_[category][0]);
        let imageSet = featuredImages_[category][0];
        if (IsFeaturedImageSetLoaded(imageSet)) {
            SetFeaturedImageSet(imageSet);
        }
        else {
            LoadFeaturedImageSetIfNotLoaded(imageSet, function() {
                SetFeaturedImageSet(imageSet);
            });
        }

        loadedEntries_ = [];
        for (let i = 0; i < allEntries_.length; i++) {
            if (allEntries_[i].tags.indexOf(category) !== -1) {
                loadedEntries_.push(allEntries_[i]);
            }
        }
        ResetPosters(loadedEntries_);
    }
}

window.onhashchange = OnHashChanged;

$(document).ready(function() {
    collectionTemplate_ = $("#collectionTemplate").html();
    $("#collectionTemplate").remove();
    recentArticleTemplate_ = $("#recentArticleTemplate").html();
    $("#recentArticleTemplate").remove();
    recentVideoTemplate_ = $("#recentVideoTemplate").html();
    $("#recentVideoTemplate").remove();

    let entriesLoaded = false;
    let featured = null;

    $.ajax({
        type: "GET",
        url: "/entries",
        contentType: "application/json",
        dataType: "json",
        async: true,
        data: "",
        success: function(data) {
            OnAllEntriesLoaded(data);
            if (featured !== null) {
                OnFeaturedEntriesLoaded(featured);
            }
            entriesLoaded = true;
        },
        error: function(error) {
            console.error(error);
        }
    });

    $.ajax({
        type: "GET",
        url: "/featured",
        contentType: "application/json",
        dataType: "json",
        async: true,
        data: "",
        success: function(data) {
            if (entriesLoaded) {
                OnFeaturedEntriesLoaded(data);
            }
            featured = data;
        },
        error: function(error) {
            console.error(error);
        }
    });

    $("#contentArrowLeftButton").on("click", function() {
        MovePosters(loadedEntries_, -1);
    })
    $("#contentArrowRightButton").on("click", function() {
        MovePosters(loadedEntries_, 1);
    })

    OnResize();
    HandleScroll();

    $("#content").css("visibility", "visible");
});
