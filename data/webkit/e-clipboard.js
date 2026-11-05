'use strict';

var EvoClipboard = {};

EvoClipboard.GetHTML = function(node, useOuterHTML)
{
	var clone = node.cloneNode(true);
	var images = Array.from(clone.querySelectorAll("img[src]"));

	if (clone.tagName == "IMG")
		images.push(clone);

	for (var image of images) {
		var src = image.getAttribute("src");

		if (!/^cid:\/\/localhost\//i.test(src))
			continue;

		try {
			image.setAttribute("src", "cid:" + decodeURIComponent(src.substring(16)));
		} catch (error) {
			if (!(error instanceof URIError))
				throw error;
			console.warn("Invalid escaped inline image reference");
		}
	}

	return useOuterHTML ? clone.outerHTML : clone.innerHTML;
};

EvoClipboard.Copy = function(event)
{
	var doc = event.currentTarget;
	var selection = doc.getSelection();

	if (event.defaultPrevented || !event.clipboardData || !selection || selection.isCollapsed || !selection.rangeCount)
		return;

	var range = selection.getRangeAt(0);
	var content = range.cloneContents();
	var ancestor = range.commonAncestorContainer;

	if (ancestor.nodeType != ancestor.ELEMENT_NODE)
		ancestor = ancestor.parentElement;

	while (ancestor && ancestor != doc.body && ancestor != doc.documentElement) {
		var wrapper = ancestor.cloneNode(false);
		wrapper.appendChild(content);
		content = wrapper;
		ancestor = ancestor.parentElement;
	}

	var container = doc.createElement("DIV");
	container.appendChild(content);
	event.clipboardData.setData("text/html", EvoClipboard.GetHTML(container, false));
	event.clipboardData.setData("text/plain", selection.toString());
	event.preventDefault();
};