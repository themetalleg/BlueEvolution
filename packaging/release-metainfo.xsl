<?xml version="1.0" encoding="UTF-8"?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
  <xsl:output method="xml" encoding="UTF-8" indent="yes"/>
  <xsl:param name="version"/>
  <xsl:param name="date"/>
  <xsl:param name="type"/>
  <xsl:template match="@*|node()">
    <xsl:copy>
      <xsl:apply-templates select="@*|node()"/>
    </xsl:copy>
  </xsl:template>
  <xsl:template match="component/releases">
    <releases>
      <release version="{$version}" date="{$date}" type="{$type}"/>
    </releases>
  </xsl:template>
</xsl:stylesheet>