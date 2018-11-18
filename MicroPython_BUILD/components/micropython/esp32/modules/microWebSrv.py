"""
The MIT License (MIT)
Copyright © 2018 Jean-Christophe Bos & HC² (www.hc2.fr)
Copyright © 2018 LoBo (https://github.com/loboris/MicroPython_ESP32_psRAM_LoBo)
"""


from   json        import loads, dumps
from   os          import stat
import _thread
import network
import time
import socket
import gc
import re

try :
    from microWebTemplate import MicroWebTemplate
except :
    pass

try :
    from microWebSocket import MicroWebSocket
except :
    pass

class MicroWebSrvRoute :
    def __init__(self, route, method, func, routeArgNames, routeRegex) :
        self.route         = route        
        self.method        = method       
        self.func          = func         
        self.routeArgNames = routeArgNames
        self.routeRegex    = routeRegex   


class MicroWebSrv :

    # ============================================================================
    # ===( Constants )============================================================
    # ============================================================================

    _indexPages = [
        "index.pyhtml",
        "index.html",
        "index.htm",
        "default.pyhtml",
        "default.html",
        "default.htm"
    ]

    _mimeTypes = {
        ".txt"   : "text/plain",
        ".htm"   : "text/html",
        ".html"  : "text/html",
        ".css"   : "text/css",
        ".csv"   : "text/csv",
        ".js"    : "application/javascript",
        ".xml"   : "application/xml",
        ".xhtml" : "application/xhtml+xml",
        ".json"  : "application/json",
        ".zip"   : "application/zip",
        ".pdf"   : "application/pdf",
        ".jpg"   : "image/jpeg",
        ".jpeg"  : "image/jpeg",
        ".png"   : "image/png",
        ".gif"   : "image/gif",
        ".svg"   : "image/svg+xml",
        ".ico"   : "image/x-icon"
    }

    _html_escape_chars = {
        "&" : "&amp;",
        '"' : "&quot;",
        "'" : "&apos;",
        ">" : "&gt;",
        "<" : "&lt;"
    }

    _pyhtmlPagesExt = '.pyhtml'

    # ============================================================================
    # ===( Class globals  )=======================================================
    # ============================================================================

    _docoratedRouteHandlers = []

    # ============================================================================
    # ===( Utils  )===============================================================
    # ============================================================================

    @classmethod
    def route(cls, url, method='GET'):
        """ Adds a route handler function to the routing list """
        def route_decorator(func):
            item = (url, method, func)
            cls._docoratedRouteHandlers.append(item)
            return func
        return route_decorator

    # ----------------------------------------------------------------------------

    @staticmethod
    def HTMLEscape(s) :
        return ''.join(MicroWebSrv._html_escape_chars.get(c, c) for c in s)

    # ----------------------------------------------------------------------------

    @staticmethod
    def _tryAllocByteArray(size) :
        for x in range(10) :
            try :
                gc.collect()
                return bytearray(size)
            except :
                pass
        return None

    # ----------------------------------------------------------------------------

    @staticmethod
    def _tryStartThread(func, args=(), stacksize=8192) :
        _ = _thread.stack_size(stacksize)
        for x in range(10) :
            try :
                gc.collect()
                th = _thread.start_new_thread("MicroWebServer", func, args)
                return th
            except :
                time.sleep_ms(100)
        return False

    # ----------------------------------------------------------------------------

    @staticmethod
    def _unquote(s) :
        r = s.split('%')
        for i in range(1, len(r)) :
            s = r[i]
            try :
                r[i] = chr(int(s[:2], 16)) + s[2:]
            except :
                r[i] = '%' + s
        return ''.join(r)

    # ----------------------------------------------------------------------------

    @staticmethod
    def _unquote_plus(s) :
        return MicroWebSrv._unquote(s.replace('+', ' '))

    # ----------------------------------------------------------------------------

    @staticmethod
    def _fileExists(path) :
        try :
            stat(path)
            return True
        except :
            return False

    # ----------------------------------------------------------------------------

    @staticmethod
    def _isPyHTMLFile(filename) :
        return filename.lower().endswith(MicroWebSrv._pyhtmlPagesExt)

    # ============================================================================
    # ===( Constructor )==========================================================
    # ============================================================================

    def __init__( self,
                  routeHandlers = [],
                  port          = 80,
                  bindIP        = '0.0.0.0',
                  webPath       = "/flash/www" ) :

        self._srvAddr       = (bindIP, port)
        self._webPath       = webPath
        self._notFoundUrl   = None
        self._started       = False
        self.thID           = None
        self.isThreaded     = False
        self._state         = "Stoped"

        self.MaxWebSocketRecvLen     = 1024
        self.WebSocketThreaded       = True
        self.WebSocketStackSize      = 4096
        self.AcceptWebSocketCallback = None

        self._routeHandlers = []
        routeHandlers += self._docoratedRouteHandlers
        for route, method, func in routeHandlers :
            routeParts = route.split('/')
            # -> ['', 'users', '<uID>', 'addresses', '<addrID>', 'test', '<anotherID>']
            routeArgNames = []
            routeRegex    = ''
            for s in routeParts :
                if s.startswith('<') and s.endswith('>') :
                    routeArgNames.append(s[1:-1])
                    routeRegex += '/(\\w*)'
                elif s :
                    routeRegex += '/' + s
            routeRegex += '$'
            # -> '/users/(\w*)/addresses/(\w*)/test/(\w*)$'
            routeRegex = re.compile(routeRegex)

            self._routeHandlers.append(MicroWebSrvRoute(route, method, func, routeArgNames, routeRegex))

    # ============================================================================
    # ===( Server Process )=======================================================
    # ============================================================================

    def _serverProcess(self) :
        self._started = True
        self._state = "Running"
        while True :
            try :
                client, cliAddr = self._server.accepted()
                if client == None:
                    if self.isThreaded:
                        notify = _thread.getnotification()
                        if notify == _thread.EXIT:
                            break
                        elif notify == _thread.SUSPEND:
                            self._state = "Suspended"
                            while _thread.wait() != _thread.RESUME:
                                pass
                            self._state = "Running"
                    # gc.collect()
                    time.sleep_ms(2)
                    continue
            except Exception as e:
                if not self.isThreaded:
                    print(e)
                break
            self._client(self, client, cliAddr)
        self._started = False
        self._state = "Stoped"
        self.thID = None

    # ============================================================================
    # ===( Functions )============================================================
    # ============================================================================

    def Start(self, threaded=True, stackSize=8192) :
        if not self._started :
            if not network.WLAN().wifiactive():
                print("WLAN not connected!")
                return
            gc.collect()
            self._server = socket.socket( socket.AF_INET,
                                          socket.SOCK_STREAM,
                                          socket.IPPROTO_TCP )
            self._server.setsockopt( socket.SOL_SOCKET,
                                     socket.SO_REUSEADDR,
                                     1 )
            self._server.bind(self._srvAddr)
            self._server.listen(1)
            self.isThreaded = threaded
            # using non-blocking socket
            self._server.settimeout(0.5)
            if threaded :
                th = MicroWebSrv._tryStartThread(self._serverProcess, stacksize=stackSize)
                if th:
                    self.thID = th
            else :
                self._serverProcess()

    # ----------------------------------------------------------------------------

    def Stop(self) :
        if self._started :
            self._server.close()
            if self.isThreaded:
                _ = _thread.notify(self.thID, _thread.EXIT)

    # ----------------------------------------------------------------------------

    def IsStarted(self) :
        return self._started

    # ----------------------------------------------------------------------------

    def threadID(self) :
        return self.thID

    # ----------------------------------------------------------------------------

    def State(self) :
        return self._state

    # ----------------------------------------------------------------------------

    def SetNotFoundPageUrl(self, url=None) :
        self._notFoundUrl = url

    # ----------------------------------------------------------------------------

    def GetMimeTypeFromFilename(self, filename) :
        filename = filename.lower()
        for ext in self._mimeTypes :
            if filename.endswith(ext) :
                return self._mimeTypes[ext]
        return None

    # ----------------------------------------------------------------------------
    
    def GetRouteHandler(self, resUrl, method) :
        if self._routeHandlers :
            #resUrl = resUrl.upper()
            if resUrl.endswith('/') :
                resUrl = resUrl[:-1]
            method = method.upper()
            for rh in self._routeHandlers :
                if rh.method == method :
                    m = rh.routeRegex.match(resUrl)
                    if m :   # found matching route?
                        if rh.routeArgNames :
                            routeArgs = {}
                            for i, name in enumerate(rh.routeArgNames) :
                                value = m.group(i+1)
                                try :
                                    value = int(value)
                                except :
                                    pass
                                routeArgs[name] = value
                            return (rh.func, routeArgs)
                        else :
                            return (rh.func, None)
        return (None, None)

    # ----------------------------------------------------------------------------

    def _physPathFromURLPath(self, urlPath) :
        if urlPath == '/' :
            for idxPage in self._indexPages :
            	physPath = self._webPath + '/' + idxPage
            	if MicroWebSrv._fileExists(physPath) :
            		return physPath
        else :
            physPath = self._webPath + urlPath
            if MicroWebSrv._fileExists(physPath) :
                return physPath
        return None

    # ============================================================================
    # ===( Class Client  )========================================================
    # ============================================================================

    class _client :

        # ------------------------------------------------------------------------

        def __init__(self, microWebSrv, socket, addr) :
            socket.settimeout(2)
            self._microWebSrv   = microWebSrv
            self._socket        = socket
            self._addr          = addr
            self._method        = None
            self._path          = None
            self._httpVer       = None
            self._resPath       = "/"
            self._queryString   = ""
            self._queryParams   = { }
            self._headers       = { }
            self._contentType   = None
            self._contentLength = 0
            
            self._processRequest()

        # ------------------------------------------------------------------------

        def _processRequest(self) :
            try :
                response = MicroWebSrv._response(self)
                if self._parseFirstLine(response) :
                    if self._parseHeader(response) :
                        upg = self._getConnUpgrade()
                        if not upg :
                            routeHandler, routeArgs = self._microWebSrv.GetRouteHandler(self._resPath, self._method)
                            if routeHandler :
                                if routeArgs is not None:
                                    routeHandler(self, response, routeArgs)
                                else:
                                    routeHandler(self, response)
                            elif self._method.upper() == "GET" :
                                filepath = self._microWebSrv._physPathFromURLPath(self._resPath)
                                if filepath :
                                    if MicroWebSrv._isPyHTMLFile(filepath) :
                                        response.WriteResponsePyHTMLFile(filepath)
                                    else :
                                        contentType = self._microWebSrv.GetMimeTypeFromFilename(filepath)
                                        if contentType :
                                            response.WriteResponseFile(filepath, contentType)
                                        else :
                                            response.WriteResponseForbidden()
                                else :
                                    response.WriteResponseNotFound()
                            else :
                                response.WriteResponseMethodNotAllowed()
                        elif upg == 'websocket' and 'MicroWebSocket' in globals() \
                             and self._microWebSrv.AcceptWebSocketCallback :
                                MicroWebSocket( socket         = self._socket,
                                                httpClient     = self,
                                                httpResponse   = response,
                                                maxRecvLen     = self._microWebSrv.MaxWebSocketRecvLen,
                                                threaded       = self._microWebSrv.WebSocketThreaded,
                                                acceptCallback = self._microWebSrv.AcceptWebSocketCallback,
                                                stackSize      = self._microWebSrv.WebSocketStackSize )
                                return
                        else :
                            response.WriteResponseNotImplemented()
                    else :
                        response.WriteResponseBadRequest()
            except :
                response.WriteResponseInternalServerError()
            try :
                self._socket.close()
            except :
                pass

        # ------------------------------------------------------------------------

        def _parseFirstLine(self, response) :
            try :
                elements = self._socket.readline().decode().strip().split()
                if len(elements) == 3 :
                    self._method  = elements[0].upper()
                    self._path    = elements[1]
                    self._httpVer = elements[2].upper()
                    elements      = self._path.split('?', 1)
                    if len(elements) > 0 :
                        self._resPath = MicroWebSrv._unquote_plus(elements[0])
                        if len(elements) > 1 :
                            self._queryString = elements[1]
                            elements = self._queryString.split('&')
                            for s in elements :
                                param = s.split('=', 1)
                                if len(param) > 0 :
                                    value = MicroWebSrv._unquote(param[1]) if len(param) > 1 else ''
                                    self._queryParams[MicroWebSrv._unquote(param[0])] = value
                    return True
            except :
                pass
            return False
    
        # ------------------------------------------------------------------------

        def _parseHeader(self, response) :
            while True :
                elements = self._socket.readline().decode().strip().split(':', 1)
                if len(elements) == 2 :
                    self._headers[elements[0].strip()] = elements[1].strip()
                elif len(elements) == 1 and len(elements[0]) == 0 :
                    if self._method == 'POST' :
                        self._contentType   = self._headers.get("Content-Type", None)
                        self._contentLength = int(self._headers.get("Content-Length", 0))
                    return True
                else :
                    return False

        # ------------------------------------------------------------------------

        def _getConnUpgrade(self) :
            if 'upgrade' in self._headers.get('Connection', '').lower() :
                return self._headers.get('Upgrade', '').lower()
            return None

        # ------------------------------------------------------------------------

        def GetServer(self) :
            return self._microWebSrv

        # ------------------------------------------------------------------------

        def GetAddr(self) :
            return self._addr

        # ------------------------------------------------------------------------

        def GetIPAddr(self) :
            return self._addr[0]

        # ------------------------------------------------------------------------

        def GetPort(self) :
            return self._addr[1]

        # ------------------------------------------------------------------------

        def GetRequestMethod(self) :
            return self._method

        # ------------------------------------------------------------------------

        def GetRequestTotalPath(self) :
            return self._path

        # ------------------------------------------------------------------------

        def GetRequestPath(self) :
            return self._resPath

        # ------------------------------------------------------------------------

        def GetRequestQueryString(self) :
            return self._queryString

        # ------------------------------------------------------------------------

        def GetRequestQueryParams(self) :
            return self._queryParams

        # ------------------------------------------------------------------------

        def GetRequestHeaders(self) :
            return self._headers

        # ------------------------------------------------------------------------

        def GetRequestContentType(self) :
            return self._contentType

        # ------------------------------------------------------------------------

        def GetRequestContentLength(self) :
            return self._contentLength

        # ------------------------------------------------------------------------

        def ReadRequestContent(self, size=None) :
            self._socket.setblocking(False)
            b = None
            try :
                if not size :
                    b = self._socket.read(self._contentLength)
                elif size > 0 :
                    b = self._socket.read(size)
            except :
                pass
            self._socket.setblocking(True)
            return b if b else b''

        # ------------------------------------------------------------------------

        def ReadRequestPostedFormData(self) :
            res  = { }
            data = self.ReadRequestContent()
            if len(data) > 0 :
                elements = data.decode().split('&')
                for s in elements :
                    param = s.split('=', 1)
                    if len(param) > 0 :
                        value = MicroWebSrv._unquote(param[1]) if len(param) > 1 else ''
                        res[MicroWebSrv._unquote(param[0])] = value
            return res

        # ------------------------------------------------------------------------

        def ReadRequestContentAsJSON(self) :
            try :
                return loads(self.ReadRequestContent())
            except :
                return None
        
    # ============================================================================
    # ===( Class Response  )======================================================
    # ============================================================================

    class _response :

        # ------------------------------------------------------------------------

        def __init__(self, client) :
            self._client = client

        # ------------------------------------------------------------------------

        def _write(self, data) :
            if type(data) == str:
                data = data.encode()
            return self._client._socket.write(data)

        # ------------------------------------------------------------------------

        def _writeFirstLine(self, code) :
            reason = self._responseCodes.get(code, ('Unknown reason', ))[0]
            self._write("HTTP/1.1 %s %s\r\n" % (code, reason))

        # ------------------------------------------------------------------------

        def _writeHeader(self, name, value) :
            self._write("%s: %s\r\n" % (name, value))

        # ------------------------------------------------------------------------

        def _writeContentTypeHeader(self, contentType, charset=None) :
            if contentType :
                ct = contentType \
                   + (("; charset=%s" % charset) if charset else "")
            else :
                ct = "application/octet-stream"
            self._writeHeader("Content-Type", ct)

        # ------------------------------------------------------------------------

        def _writeServerHeader(self) :
            self._writeHeader("Server", "MicroWebSrv by JC`zic")

        # ------------------------------------------------------------------------

        def _writeEndHeader(self) :
            self._write("\r\n")

        # ------------------------------------------------------------------------

        def _writeBeforeContent(self, code, headers, contentType, contentCharset, contentLength) :
            self._writeFirstLine(code)
            if isinstance(headers, dict) :
                for header in headers :
                    self._writeHeader(header, headers[header])
            if contentLength > 0 :
                self._writeContentTypeHeader(contentType, contentCharset)
                self._writeHeader("Content-Length", contentLength)
            self._writeServerHeader()
            self._writeHeader("Connection", "close")
            self._writeEndHeader()

        # ------------------------------------------------------------------------

        def WriteSwitchProto(self, upgrade, headers=None) :
            self._writeFirstLine(101)
            self._writeHeader("Connection", "Upgrade")
            self._writeHeader("Upgrade",    upgrade)
            if isinstance(headers, dict) :
                for header in headers :
                    self._writeHeader(header, headers[header])
            self._writeServerHeader()
            self._writeEndHeader()

        # ------------------------------------------------------------------------

        def WriteResponse(self, code, headers, contentType, contentCharset, content) :
            try :
                contentLength = len(content) if content else 0
                self._writeBeforeContent(code, headers, contentType, contentCharset, contentLength)
                if contentLength > 0 :
                    self._write(content)
                return True
            except :
                return False

        # ------------------------------------------------------------------------

        def WriteResponsePyHTMLFile(self, filepath, headers=None) :
            if 'MicroWebTemplate' in globals() :
                with open(filepath, 'r') as file :
                    code = file.read()
                gc.collect()
                mWebTmpl = MicroWebTemplate(code, escapeStrFunc=MicroWebSrv.HTMLEscape, filepath=filepath)
                try :
                    tmplResult = mWebTmpl.Execute()
                    return self.WriteResponse(200, headers, "text/html", "UTF-8", tmplResult)
                except Exception as ex :
                    return self.WriteResponse( 500,
    	                                       None,
    	                                       "text/html",
    	                                       "UTF-8",
    	                                       self._execErrCtnTmpl % {
    	                                            'module'  : 'PyHTML',
    	                                            'message' : str(ex)
    	                                       } )
            return self.WriteResponseNotImplemented()

        # ------------------------------------------------------------------------

        def WriteResponseFile(self, filepath, contentType=None, headers=None) :
            try :
                size = stat(filepath)[6]
                if size > 0 :
                    with open(filepath, 'rb') as file :
                        self._writeBeforeContent(200, headers, contentType, None, size)
                        buf = MicroWebSrv._tryAllocByteArray(1024)
                        if buf :
                            while size > 0 :
                                x = file.readinto(buf)
                                if x < len(buf) :
                                    buf = memoryview(buf)[:x]
                                self._write(buf)
                                size -= x
                            return True
                        self.WriteResponseInternalServerError()
                        return False
            except :
                pass
            self.WriteResponseNotFound()
            return False

        # ------------------------------------------------------------------------

        def WriteResponseFileAttachment(self, filepath, attachmentName, headers=None) :
            if not isinstance(headers, dict) :
                headers = { }
            headers["Content-Disposition"] = "attachment; filename=\"%s\"" % attachmentName
            return self.WriteResponseFile(filepath, None, headers)

        # ------------------------------------------------------------------------

        def WriteResponseOk(self, headers=None, contentType=None, contentCharset=None, content=None) :
            return self.WriteResponse(200, headers, contentType, contentCharset, content)

        # ------------------------------------------------------------------------

        def WriteResponseJSONOk(self, obj=None, headers=None) :
            return self.WriteResponse(200, headers, "application/json", "UTF-8", dumps(obj))

        # ------------------------------------------------------------------------

        def WriteResponseRedirect(self, location) :
            headers = { "Location" : location }
            return self.WriteResponse(302, headers, None, None, None)

        # ------------------------------------------------------------------------

        def WriteResponseError(self, code) :
            responseCode = self._responseCodes.get(code, ('Unknown reason', ''))
            return self.WriteResponse( code,
                                       None,
                                       "text/html",
                                       "UTF-8",
                                       self._errCtnTmpl % {
                                            'code'    : code,
                                            'reason'  : responseCode[0],
                                            'message' : responseCode[1]
                                       } )

        # ------------------------------------------------------------------------

        def WriteResponseJSONError(self, code, obj=None) :
            return self.WriteResponse( code,
                                       None,
                                       "application/json",
                                       "UTF-8",
                                       dumps(obj if obj else { }) )

        # ------------------------------------------------------------------------

        def WriteResponseBadRequest(self) :
            return self.WriteResponseError(400)

        # ------------------------------------------------------------------------

        def WriteResponseForbidden(self) :
            return self.WriteResponseError(403)

        # ------------------------------------------------------------------------

        def WriteResponseNotFound(self) :
            if self._client._microWebSrv._notFoundUrl :
                self.WriteResponseRedirect(self._client._microWebSrv._notFoundUrl)
            else :
                return self.WriteResponseError(404)

        # ------------------------------------------------------------------------

        def WriteResponseMethodNotAllowed(self) :
            return self.WriteResponseError(405)

        # ------------------------------------------------------------------------

        def WriteResponseInternalServerError(self) :
            return self.WriteResponseError(500)

        # ------------------------------------------------------------------------

        def WriteResponseNotImplemented(self) :
            return self.WriteResponseError(501)

        # ------------------------------------------------------------------------

        _errCtnTmpl = """\
        <html>
            <head>
                <title>Error</title>
            </head>
            <body>
                <h1>%(code)d %(reason)s</h1>
                %(message)s
            </body>
        </html>
        """

        # ------------------------------------------------------------------------

        _execErrCtnTmpl = """\
        <html>
            <head>
                <title>Page execution error</title>
            </head>
            <body>
                <h1>%(module)s page execution error</h1>
                %(message)s
            </body>
        </html>
        """

        # ------------------------------------------------------------------------

        _responseCodes = {
            100: ('Continue', 'Request received, please continue'),
            101: ('Switching Protocols',
                  'Switching to new protocol; obey Upgrade header'),

            200: ('OK', 'Request fulfilled, document follows'),
            201: ('Created', 'Document created, URL follows'),
            202: ('Accepted',
                  'Request accepted, processing continues off-line'),
            203: ('Non-Authoritative Information', 'Request fulfilled from cache'),
            204: ('No Content', 'Request fulfilled, nothing follows'),
            205: ('Reset Content', 'Clear input form for further input.'),
            206: ('Partial Content', 'Partial content follows.'),

            300: ('Multiple Choices',
                  'Object has several resources -- see URI list'),
            301: ('Moved Permanently', 'Object moved permanently -- see URI list'),
            302: ('Found', 'Object moved temporarily -- see URI list'),
            303: ('See Other', 'Object moved -- see Method and URL list'),
            304: ('Not Modified',
                  'Document has not changed since given time'),
            305: ('Use Proxy',
                  'You must use proxy specified in Location to access this '
                  'resource.'),
            307: ('Temporary Redirect',
                  'Object moved temporarily -- see URI list'),

            400: ('Bad Request',
                  'Bad request syntax or unsupported method'),
            401: ('Unauthorized',
                  'No permission -- see authorization schemes'),
            402: ('Payment Required',
                  'No payment -- see charging schemes'),
            403: ('Forbidden',
                  'Request forbidden -- authorization will not help'),
            404: ('Not Found', 'Nothing matches the given URI'),
            405: ('Method Not Allowed',
                  'Specified method is invalid for this resource.'),
            406: ('Not Acceptable', 'URI not available in preferred format.'),
            407: ('Proxy Authentication Required', 'You must authenticate with '
                  'this proxy before proceeding.'),
            408: ('Request Timeout', 'Request timed out; try again later.'),
            409: ('Conflict', 'Request conflict.'),
            410: ('Gone',
                  'URI no longer exists and has been permanently removed.'),
            411: ('Length Required', 'Client must specify Content-Length.'),
            412: ('Precondition Failed', 'Precondition in headers is false.'),
            413: ('Request Entity Too Large', 'Entity is too large.'),
            414: ('Request-URI Too Long', 'URI is too long.'),
            415: ('Unsupported Media Type', 'Entity body in unsupported format.'),
            416: ('Requested Range Not Satisfiable',
                  'Cannot satisfy request range.'),
            417: ('Expectation Failed',
                  'Expect condition could not be satisfied.'),

            500: ('Internal Server Error', 'Server got itself in trouble'),
            501: ('Not Implemented',
                  'Server does not support this operation'),
            502: ('Bad Gateway', 'Invalid responses from another server/proxy.'),
            503: ('Service Unavailable',
                  'The server cannot process the request due to a high load'),
            504: ('Gateway Timeout',
                  'The gateway server did not receive a timely response'),
            505: ('HTTP Version Not Supported', 'Cannot fulfill request.'),
        }

    # ============================================================================
    # ============================================================================
    # ============================================================================

