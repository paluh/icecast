<xsl:stylesheet xmlns:xsl = "http://www.w3.org/1999/XSL/Transform" version = "1.0" >
<xsl:output method="html" indent="yes" />
<xsl:template match = "/icestats" >
<html>
<head>
<title>Icecast Streaming Media Server</title>
<link rel="stylesheet" type="text/css" href="/style.css" />
</head>
<body bgcolor="black">
	<center>
	<table border="0" cellpadding="1" cellspacing="3">
	<tr>        
	    <td align="center">
		<a class="nav" href="listmounts.xsl">List MountPoints</a> | 
        	<a class="nav" href="moveclients.xsl">Move Listeners</a> | 
        	<a class="nav" href="stats.xsl">Stats</a> | 
        	<a class="nav" href="/status.xsl">Status Page</a>
	    </td></tr>
	</table>
	</center>
<table border="0" width="90%%">
<tr>
<td width="50"></td>
<td>
<h2>List Mountpoints</h2>
<div class="roundcont">
<div class="roundtop">
<img src="/corner_topleft.jpg" class="corner" style="display: none" />
</div>
<div class="newscontent">
<xsl:for-each select="source">
<h3>
<xsl:if test="server_name"><xsl:value-of select="server_name" /> </xsl:if>
(<xsl:value-of select="@mount" />)</h3>
	<table border="0" cellpadding="1" cellspacing="5" bgcolor="444444">
	<tr>        
	    <td align="center">
		<a class="nav2" href="listclients.xsl?mount={@mount}">Show Listeners</a> | 
        	<a class="nav2" href="moveclients.xsl?mount={@mount}">Move Listeners</a> | 
        	<a class="nav2" href="killsource.xsl?mount={@mount}">Kill Source</a>
	    </td></tr>
	</table>
<br></br>
<p><xsl:value-of select="listeners" /> Listener(s)</p>
<br></br>
</xsl:for-each>
</div>
<div class="roundbottom">
<img src="/corner_bottomleft.jpg" class="corner" style="display: none" />
</div>
</div>
<br></br><br></br>
</td>
<td width="50"></td>
</tr>
</table>
<div class="poster">
<img align="left" src="/icecast.png" />Support icecast development at <a class="nav" href="http://www.icecast.org">www.icecast.org</a></div>
</body>
</html>
</xsl:template>
</xsl:stylesheet>
