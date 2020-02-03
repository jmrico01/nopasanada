"use strict";

// const IMAGE_BASE_URL = "https://nopasanada.s3.amazonaws.com";
const IMAGE_BASE_URL = ".";

const FEATURED_IMAGE_FADE_MS = 400;
const IMAGE_ANIM_MS = 250;

const HOMEPAGE_CATEGORY = "home";

let allEntries_ = null;
let featuredEntries_ = null;
let loadedEntries_ = null; // TODO revisit name + use-case, will need more groups of stuff

let featuredImages_ = null;
let posterImages_ = null;

let posterTemplate_ = null;
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

    return HOMEPAGE_CATEGORY;
}

function SetFeaturedInfo(entry)
{
    $("#featuredPretitle").html(entry.pretitle);
    $("#featuredTitle a").html(entry.title);
    $("#featuredTitle a").attr("href", entry.uri);
    $("#featuredDecoration").html(entry.decoration);
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

function GetFeaturedImageId(category, index, imageIndex)
{
    return "featuredImage-" + category + "-" + index.toString() + "-" + imageIndex.toString();
}

function ResetImageSize(category, index, imageIndex)
{
    let image = featuredImages_[category][index][imageIndex];
    let $image = $("#" + GetFeaturedImageId(category, index, imageIndex));

    let aspect = window.innerWidth / window.innerHeight;
    let imageAspect = image.width / image.height;
    if (aspect > imageAspect) {
        $image.width("100%");
        $image.height("auto");
    }
    else {
        $image.width("auto");
        $image.height("100%");
        let imageWidth = document.documentElement.clientHeight * imageAspect;
        let marginX = (imageWidth - document.documentElement.clientWidth) / 2;
        $image.css("margin-left", -marginX);
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
        $("#screenPosters").css("height", "auto");
    }

    let aspect = window.innerWidth / window.innerHeight;
    for (let category in featuredImages_) {
        for (let i = 0; i < featuredImages_[category].length; i++) {
            for (let j = 0; j < featuredImages_[category][i].length; j++) {
                ResetImageSize(category, i, j);
            }
        }
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

function IsFeaturedImageSetLoaded(category, featuredIndex)
{
    if (featuredIndex >= featuredImages_[category].length) {
        console.error("featuredIndex " + featuredIndex + " out of bounds for category " + category);
        return false;
    }

    const imageSet = featuredImages_[category][featuredIndex];
    if (imageSet.length === 0) {
        console.error("imageSet of length 0 for " + category + ", " + featuredIndex);
        return false;
    }
    for (let i = 0; i < imageSet.length; i++) {
        if (!imageSet[i].hasOwnProperty("_npn_loaded") || !imageSet[i]._npn_loaded) {
            return false;
        }
    }
    return true;
}

function SetFeaturedImageSet(category, index)
{
    if (imgCycleInterval_ !== null) {
        clearInterval(imgCycleInterval_);
        imgCycleInterval_ = null;
    }

    let $currentActive = $("#landingImageCycler img.active");
    let $currentTransition = $("#landingImageCycler img.transition");
    let $nextActive = $("#" + GetFeaturedImageId(category, index, 0));
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

    // let counter = 0;
    // let counterDir = 1;
    // imgCycleInterval_ = setInterval(function() {
    //     let numImages = featuredEntries[category].images.length;
    //     if (numImages === 1) {
    //         return;
    //     }
    //     let imageId = "#featuredImage-" + category + "-" + counter;
    //     let $currentActive = $("#landingImageCycler img.active");
    //     $currentActive.removeClass("active");
    //     let $featuredImage = $(imageId).addClass("active");
    //     if (counter >= numImages - 1) {
    //         counterDir = -1;
    //     }
    //     else if (counter <= 0) {
    //         counterDir = 1;
    //     }
    //     counter += counterDir;
    // }, IMAGE_ANIM_MS);
}

function OnFeaturedImageSetLoaded(category, index)
{
    let imageSet = featuredImages_[category][index];
    for (let i = 0; i < imageSet.length; i++) {
        let imageId = GetFeaturedImageId(category, index, i);
        $("#" + imageId).attr("src", imageSet[i].src);
        ResetImageSize(category, index, i);
    }
}

function LoadFeaturedImageSetIfNotLoaded(category, index, callback)
{
    if (IsFeaturedImageSetLoaded(category, index)) {
        callback();
        return;
    }
    const nextImageSet = featuredEntries_[category][index].images;
    for (let i = 0; i < nextImageSet.length; i++) {
        if (featuredImages_[category][index][i]._npn_loading) {
            continue;
        }
        featuredImages_[category][index][i]._npn_loading = true;
        featuredImages_[category][index][i].onload = function() {
            featuredImages_[category][index][i]._npn_loaded = true;
            featuredImages_[category][index][i]._npn_loading = false;
            if (IsFeaturedImageSetLoaded(category, index)) {
                OnFeaturedImageSetLoaded(category, index);
                callback();
            }
        };
        featuredImages_[category][index][i].src = IMAGE_BASE_URL + nextImageSet[i];
    }
}

function LoadAllFeaturedImageSets()
{
    for (let category in featuredImages_) {
        for (let i = 0; i < featuredImages_[category].length; i++) {
            if (IsFeaturedImageSetLoaded(category, i)) {
                continue;
            }
            LoadFeaturedImageSetIfNotLoaded(category, i, function() {
                // pretty dumb, sure... but whatever
                LoadAllFeaturedImageSets();
            });
        }
    }
}

function OnFeaturedEntriesLoaded(featured)
{
    featuredEntries_ = {};
    featuredImages_ = {};
    for (let category in featured) {
        featuredEntries_[category] = [];
        featuredImages_[category] = [];
        for (let i = 0; i < featured[category].length; i++) {
            const uri = featured[category][i];
            for (let j = 0; j < allEntries_.length; j++) {
                if (allEntries_[j].uri === uri) {
                    let entry = allEntries_[j].featuredInfo;
                    entry.uri = uri;
                    let images = [];
                    for (let k = 0; k < entry.images.length; k++) {
                        let img = new Image;
                        img._npn_loaded = false;
                        img._npn_loading = false;
                        images.push(img);
                        let imageId = GetFeaturedImageId(category, featuredImages_[category].length, k);
                        $("#landingImageCycler").append("<img id=\"" + imageId + "\" class=\"featuredImage\" src=\"\">");
                    }
                    featuredEntries_[category].push(entry);
                    featuredImages_[category].push(images);
                    break;
                }
            }
        }
    }

    $(".featuredImage").hide();

    let startCategory = GetCurrentCategory();
    SetFeaturedInfo(featuredEntries_[startCategory][0]);
    LoadFeaturedImageSetIfNotLoaded(startCategory, 0, function() {
        SetFeaturedImageSet(startCategory, 0);
        $(".featuredImage").show();
        LoadAllFeaturedImageSets();
        // TODO prioritize featured image loading over these
        ResetPosters(loadedEntries_);
    });
}

function OnHashChanged()
{
    let category = GetCurrentCategory();
    if (category !== prevCategory_) {
        prevCategory_ = category;
        SetFeaturedInfo(featuredEntries_[category][0]);
        if (IsFeaturedImageSetLoaded(category, 0)) {
            SetFeaturedImageSet(category, 0);
        }
        else {
            LoadFeaturedImageSetIfNotLoaded(category, 0, function() {
                SetFeaturedImageSet(category, 0);
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
    posterTemplate_ = $("#posterTemplate").html();
    $("#posterTemplate").remove();

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
            allEntries_ = data;
            loadedEntries_ = [];
            let currentCategory = GetCurrentCategory();
            for (let i = 0; i < allEntries_.length; i++) {
                allEntries_[i].title = allEntries_[i].title.toUpperCase();
                let entryTags = allEntries_[i].tags;
                for (let j = 0; j < entryTags.length; j++) {
                    if (entryTags[j] === currentCategory) {
                        loadedEntries_.push(allEntries_[i]);
                    }
                }
            }

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
