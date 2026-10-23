'use strict';

var EvoClipboard = {};

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
	event.clipboardData.setData("text/html", container.innerHTML);
	event.clipboardData.setData("text/plain", selection.toString());
	event.preventDefault();
};