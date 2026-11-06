<?xml version="1.0" encoding="UTF-8"?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
  <xsl:output method="xml" encoding="UTF-8"/>
  <xsl:template match="@*|node()">
    <xsl:copy><xsl:apply-templates select="@*|node()"/></xsl:copy>
  </xsl:template>
  <xsl:template match="component">
    <xsl:copy>
      <xsl:apply-templates select="@*|node()[not(self::bundle)]"/>
      <bundle type="flatpak">
        <xsl:text>app/</xsl:text>
        <xsl:value-of select="substring-before(id, '.desktop')"/>
        <xsl:text>/x86_64/</xsl:text>
        <xsl:choose>
          <xsl:when test="id='io.github.themetalleg.BlueEvolution.Beta.desktop'">beta</xsl:when>
          <xsl:otherwise>stable</xsl:otherwise>
        </xsl:choose>
      </bundle>
    </xsl:copy>
  </xsl:template>
</xsl:stylesheet>
