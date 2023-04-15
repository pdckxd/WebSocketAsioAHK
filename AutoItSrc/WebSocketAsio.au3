#include-once

Global Const $c_ret_size_t = @AutoItX64 ? "UINT64" : "UINT:cdecl"
Global Const $c_ret_none_t = @AutoItX64 ? "None" : "None:cdecl"
Global Const $hWEBSOCKDLL = DllOpen("C:\Users\danpo\OneDrive\Documents\GitHub\WebSocketAsio\build\Debug\WebSocketAsio-x86.dll")


; #FUNCTION# ====================================================================================================================
; Name ..........: _WS_EnableVerbose
; Description ...: Enable or disable verbose output from WebsocketAsio-[x86|x64].dll internally
; Syntax ........: _WS_EnableVerbose($bVerbose)
; Parameters ....: $bVerbose  - True: Enabled, False: Disabled
; Return values .: None
; Author ........: Danp2
; Modified ......:
; Remarks .......:
; Related .......:
; Link ..........:
; Example .......: No
; ===============================================================================================================================
Func _WS_EnableVerbose($bVerbose)
	; EXPORT void enable_verbose(intptr_t enabled);
	Local $aCall = DllCall($hWEBSOCKDLL, $c_ret_none_t, "enable_verbose", _
			"INT_PTR", $bVerbose ? 1 : 0)
	If @error Then Return SetError(@error, @extended, -1)
	Return $aCall[0]
EndFunc   ;==>_WS_EnableVerbose

; #FUNCTION# ====================================================================================================================
; Name ..........: _WS_Connect
; Description ...: Connect to websocket server.
; Syntax ........: _WS_Connect($sURI)
; Parameters ....: $sURI - Websocket URI. e.g. ws://localhost:8199/ws
; Return values .: Success - 1
;  Failure - -1
; Author ........: Danp2
; Modified ......:
; Remarks .......:
; Related .......:
; Link ..........:
; Example .......: No
; ===============================================================================================================================
Func _WS_Connect($sURI, $iTimeout = 5000)
	; EXPORT size_t websocket_connect(const wchar_t *szServer);
	Local $aCall = DllCall($hWEBSOCKDLL, $c_ret_size_t, "websocket_connect", _
			"wstr", $sURI)
	If @error Then Return SetError(@error, @extended, -1)

	If $iTimeout Then
		Local $hTimer = TimerInit()
		Do
			If TimerDiff($hTimer) > $iTimeout Then Return SetError(2, 0, -1)
			Sleep(100)
		Until _WS_IsConnected()
	EndIf

	Return $aCall[0]
EndFunc   ;==>_WS_Connect

; #FUNCTION# ====================================================================================================================
; Name ..........: _WS_Send
; Description ...: Send data to remote websocket server
; Syntax ........: _WS_Send($sData)
; Parameters ....: $sData - Data to be sent
; Return values .: Success - 1
;  Failure - 0
; Author ........: Danp2
; Modified ......:
; Remarks .......:
; Related .......:
; Link ..........:
; Example .......: No
; ===============================================================================================================================
Func _WS_Send($sData)
	; EXPORT size_t websocket_send(const wchar_t *szMessage, size_t dwLen, bool isBinary);
	Local $iLength = StringLen($sData)
	Local $aCall = DllCall($hWEBSOCKDLL, $c_ret_size_t, "websocket_send", _
			"wstr", $sData, "USHORT", $iLength, "BOOL", 0)
	If @error Then Return SetError(@error, @extended, -1)
	Return $aCall[0]
EndFunc   ;==>_WS_Send

; #FUNCTION# ====================================================================================================================
; Name ..........: _WS_Disconnect
; Description ...: Close connection to websocket server.
; Syntax ........: _WS_Disconnect()
; Parameters ....: None
; Return values .: Success - 1
;  Failure - 0
; Author ........: Danp2
; Modified ......:
; Remarks .......:
; Related .......:
; Link ..........:
; Example .......: No
; ===============================================================================================================================
Func _WS_Disconnect()
	; EXPORT size_t websocket_disconnect();
	Local $aCall = DllCall($hWEBSOCKDLL, $c_ret_size_t, "websocket_disconnect")
	If @error Then Return SetError(@error, @extended, -1)
	Return $aCall[0]
EndFunc   ;==>_WS_Disconnect

; #FUNCTION# ====================================================================================================================
; Name ..........: _WS_IsConnected
; Description ...: Returns a boolean of the connection status.
; Syntax ........: _WS_IsConnected()
; Parameters ....: None
; Return values .: Boolean response
; Author ........: Danp2
; Modified ......:
; Remarks .......:
; Related .......:
; Link ..........:
; Example .......: No
; ===============================================================================================================================
Func _WS_IsConnected()
	; EXPORT size_t websocket_isconnected();
	Local $aCall = DllCall($hWEBSOCKDLL, $c_ret_size_t, "websocket_isconnected")
	If @error Then Return SetError(@error, @extended, -1)
	Return ($aCall[0] = 1)
EndFunc   ;==>_WS_IsConnected

; #FUNCTION# ====================================================================================================================
; Name ..........: _WS_RegisterOnConnectCB
; Description ...: Register a function defined in user's AutoIt script to be called after connection is successfully established.
; Syntax ........: _WS_RegisterOnConnectCB($hFunc)
; Parameters ....: $hFunc - Handle obtained via DllCallbackRegister
; Return values .: None
; Author ........: Danp2
; Modified ......:
; Remarks .......:
; Related .......:
; Link ..........:
; Example .......: No
; ===============================================================================================================================
Func _WS_RegisterOnConnectCB($hFunc)
	Local $aCall = DllCall($hWEBSOCKDLL, $c_ret_size_t, "websocket_register_on_connect_cb", _
			"ULONG", DllCallbackGetPtr($hFunc))
	If @error Then Return SetError(@error, @extended, -1)
	Return $aCall[0]
EndFunc   ;==>_WS_RegisterOnConnectCB

; #FUNCTION# ====================================================================================================================
; Name ..........: _WS_RegisterOnFailCB
; Description ...: Register a function defined in user's AutoIt script to be called whenever websocket error occurs.
; Syntax ........: _WS_RegisterOnFailCB($hFunc)
; Parameters ....: $hFunc - Handle obtained via DllCallbackRegister
; Return values .: None
; Author ........: Danp2
; Modified ......:
; Remarks .......:
; Related .......:
; Link ..........:
; Example .......: No
; ===============================================================================================================================
Func _WS_RegisterOnFailCB($hFunc)
	Local $aCall = DllCall($hWEBSOCKDLL, $c_ret_size_t, "websocket_register_on_fail_cb", _
			"ULONG", DllCallbackGetPtr($hFunc))
	If @error Then Return SetError(@error, @extended, -1)
	Return $aCall[0]
EndFunc   ;==>_WS_RegisterOnFailCB

; #FUNCTION# ====================================================================================================================
; Name ..........: _WS_RegisterOnDisconnectCB
; Description ...: Register a function defined in user's AutoIt script to be called after user actively calls _WS_Disconnect.
; Syntax ........: _WS_RegisterOnDisconnectCB($hFunc)
; Parameters ....: $hFunc - Handle obtained via DllCallbackRegister
; Return values .: None
; Author ........: Danp2
; Modified ......:
; Remarks .......:
; Related .......:
; Link ..........:
; Example .......: No
; ===============================================================================================================================
Func _WS_RegisterOnDisconnectCB($hFunc)
	Local $aCall = DllCall($hWEBSOCKDLL, $c_ret_size_t, "websocket_register_on_disconnect_cb", _
			"ULONG", DllCallbackGetPtr($hFunc))
	If @error Then Return SetError(@error, @extended, -1)
	Return $aCall[0]
EndFunc   ;==>_WS_RegisterOnDisconnectCB

; #FUNCTION# ====================================================================================================================
; Name ..........: _WS_RegisterOnDataCB
; Description ...: Register a function defined in user's AutoIt script to be called after data from server reaches client.
; Syntax ........: _WS_RegisterOnDataCB($hFunc)
; Parameters ....: $hFunc - Handle obtained via DllCallbackRegister
; Return values .: None
; Author ........: Danp2
; Modified ......:
; Remarks .......:
; Related .......:
; Link ..........:
; Example .......: No
; ===============================================================================================================================
Func _WS_RegisterOnDataCB($hFunc)
	Local $aCall = DllCall($hWEBSOCKDLL, $c_ret_size_t, "websocket_register_on_data_cb", _
			"ULONG", DllCallbackGetPtr($hFunc))
	If @error Then Return SetError(@error, @extended, -1)
	Return $aCall[0]
EndFunc   ;==>_WS_RegisterOnDataCB

