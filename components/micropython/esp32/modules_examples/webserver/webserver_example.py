
from    microWebSrv import MicroWebSrv
import _thread

# set to True to print WebSocket messages
WS_messages = False

# ----------------------------------------------------------------------------

def _httpHandlerTestGet(httpClient, httpResponse) :
    content = """\
    <!DOCTYPE html>
    <html lang=fr>
        <head>
            <meta charset="UTF-8" />
            <title>TEST GET</title>
        </head>
        <body>
            <h1>TEST GET</h1>
            Client IP address = %s
            <br />
            <form action="/TEST" method="post" accept-charset="ISO-8859-1">
                First name: <input type="text" name="firstname"><br />
                Last name: <input type="text" name="lastname"><br />
                <input type="submit" value="Submit">
            </form>
        </body>
    </html>
    """ % httpClient.GetIPAddr()
    httpResponse.WriteResponseOk( headers		 = None,
                                    contentType	 = "text/html",
                                    contentCharset = "UTF-8",
                                    content 		 = content )

def _httpHandlerTestPost(httpClient, httpResponse) :
    formData  = httpClient.ReadRequestPostedFormData()
    firstname = formData["firstname"]
    lastname  = formData["lastname"]
    content   = """\
    <!DOCTYPE html>
    <html lang=fr>
        <head>
            <meta charset="UTF-8" />
            <title>TEST POST</title>
        </head>
        <body>
            <h1>TEST POST</h1>
            Firstname = %s<br />
            Lastname = %s<br />
        </body>
    </html>
    """ % ( MicroWebSrv.HTMLEscape(firstname),
            MicroWebSrv.HTMLEscape(lastname) )
    httpResponse.WriteResponseOk( headers		 = None,
                                    contentType	 = "text/html",
                                    contentCharset = "UTF-8",
                                    content 		 = content )

# ----------------------------------------------------------------------------

def _acceptWebSocketCallback(webSocket, httpClient) :
    if WS_messages:
        print("WS ACCEPT")
        _thread.list()
    webSocket.RecvTextCallback   = _recvTextCallback
    webSocket.RecvBinaryCallback = _recvBinaryCallback
    webSocket.ClosedCallback 	 = _closedCallback

def _recvTextCallback(webSocket, msg) :
    if WS_messages:
        print("WS RECV TEXT : %s" % msg)
    webSocket.SendText("Reply for %s" % msg)

def _recvBinaryCallback(webSocket, data) :
    if WS_messages:
        print("WS RECV DATA : %s" % data)

def _closedCallback(webSocket) :
    if WS_messages:
        _thread.list()
        print("WS CLOSED")

# ----------------------------------------------------------------------------

routeHandlers = [
    ( "/test",	"GET",	_httpHandlerTestGet ),
    ( "/test",	"POST",	_httpHandlerTestPost )
]

srv = MicroWebSrv(routeHandlers=routeHandlers)

# ------------------------------------------------------
# WebSocket configuration
srv.MaxWebSocketRecvLen     = 256
# Run WebSocket in thread
srv.WebSocketThreaded       = True
# If WS is running in thread, set the thread stack size
# For this example 4096 is enough, for more complex
# webSocket handling you may need to increase this size
srv.WebSocketStackSize      = 4096
srv.AcceptWebSocketCallback = _acceptWebSocketCallback
# ------------------------------------------------------

srv.Start(threaded=True)

# ----------------------------------------------------------------------------
