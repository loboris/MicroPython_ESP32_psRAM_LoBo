
from   microWebSrv import MicroWebSrv
import _thread

# set to True to print WebSocket messages
WS_messages = False

# =================================================
# Recommended configuration:
#   - run microWebServer in thread
#   - do NOT run MicroWebSocket in thread
# =================================================
# Run microWebServer in thread
srv_run_in_thread = True
# Run microWebSocket in thread
ws_run_in_thread = False


# ----------------------------------------------------------------------
# To test *.pyhtml* rendered pages goto <IP>/test.pyhtml in your browser
# ----------------------------------------------------------------------

# ------------------------------------------------------------
# To test websocket page goto <IP>/wstest.html in your browser
# ------------------------------------------------------------


# -----------------------------------------------------
# Define microWebServer route handlers using decorators
# -----------------------------------------------------

# <IP>/TEST
@MicroWebSrv.route('/TEST')
def _httpHandlerTestGet(httpClient, httpResponse) :
    content = """\
    <!DOCTYPE html>
    <html lang=en>
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

@MicroWebSrv.route('/TEST', 'POST')
def _httpHandlerTestPost(httpClient, httpResponse) :
    formData  = httpClient.ReadRequestPostedFormData()
    firstname = formData["firstname"]
    lastname  = formData["lastname"]
    content   = """\
    <!DOCTYPE html>
    <html lang=en>
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

@MicroWebSrv.route('/edit/<index>')             # <IP>/edit/123           ->   args['index']=123
@MicroWebSrv.route('/edit/<index>/abc/<foo>')   # <IP>/edit/123/abc/bar   ->   args['index']=123  args['foo']='bar'
@MicroWebSrv.route('/edit')                     # <IP>/edit               ->   args={}
def _httpHandlerEditWithArgs(httpClient, httpResponse, args={}) :
    content = """\
    <!DOCTYPE html>
    <html lang=en>
        <head>
            <meta charset="UTF-8" />
            <title>TEST EDIT</title>
        </head>
        <body>
    """
    content += "<h1>EDIT item with {} variable arguments</h1>"\
        .format(len(args))

    if 'index' in args :
        content += "<p>index = {}</p>".format(args['index'])

    if 'foo' in args :
        content += "<p>foo = {}</p>".format(args['foo'])

    content += """
        </body>
    </html>
    """
    httpResponse.WriteResponseOk( headers		 = None,
                                    contentType	 = "text/html",
                                    contentCharset = "UTF-8",
                                    content 		 = content )

# ------------------------------------

# === MicroWebSocket callbacks ===

def _acceptWebSocketCallback(webSocket, httpClient) :
    if WS_messages:
        print("WS ACCEPT")
        if ws_run_in_thread or srv_run_in_thread:
            # Print thread list so that we can monitor maximum stack size
            # of WebServer thread and WebSocket thread if any is used
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
        if ws_run_in_thread or srv_run_in_thread:
            _thread.list()
        print("WS CLOSED")

# ----------------------------------------------------------------------------

# You can set the path to the www directory, default is '/flash/www'
#srv = MicroWebSrv(webPath='www_path')
srv = MicroWebSrv()

# ------------------------------------------------------
# WebSocket configuration
srv.MaxWebSocketRecvLen     = 256

# Run WebSocket in thread or not
srv.WebSocketThreaded       = ws_run_in_thread
# If WebSocket is running in thread, set the thread stack size
#    For this example 4096 should be enough, for more complex
#    webSocket handling you may need to increase this size
# If WebSocketS is NOT running in thread, and WebServer IS running in thread
# make shure WebServer has enough stack size to handle also the WebSocket requests
srv.WebSocketStackSize      = 4096
srv.AcceptWebSocketCallback = _acceptWebSocketCallback
# ------------------------------------------------------

# If WebSocketS used and NOT running in thread, and WebServer IS running in thread
# make shure WebServer has enough stack size to handle also the WebSocket requests
srv.Start(threaded=srv_run_in_thread, stackSize=8192)

# ----------------------------------------------------------------------------
