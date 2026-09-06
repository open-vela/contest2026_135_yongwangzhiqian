// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion.gateway

import com.shaniu.companion.protocol.ConsoleWireV1
import okhttp3.Interceptor
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Protocol
import okhttp3.Request
import okhttp3.Response
import okhttp3.ResponseBody.Companion.toResponseBody
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import java.net.URI
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

class OkHttpConsoleGatewaySessionTest {
    @Test
    fun missingCredentialStopsBeforeNetworkDispatch() {
        var dispatches = 0
        val client = clientWithResponse { request ->
            dispatches += 1
            response(request, 200, "{}")
        }
        val session = session(GatewayAccessTokenProvider { null }, client)

        val error = assertThrows(GatewayTransportException::class.java) {
            session.execute(getRequest("https://gateway.example/console/v1/test"))
        }

        assertEquals(GatewayFailureReason.CREDENTIALS_UNAVAILABLE, error.reason)
        assertFalse(error.retryable)
        assertEquals(0, dispatches)
        session.close()
    }

    @Test
    fun authenticatedRequestIsSameOriginAndBodyIsBounded() {
        var observed: Request? = null
        val client = clientWithResponse { request ->
            observed = request
            response(request, 200, "{\"protocol\":\"console-v1\"}")
        }
        val session = session(tokenProvider(), client)

        val result = session.execute(getRequest("https://gateway.example/console/v1/test"))

        assertEquals(200, result.statusCode)
        assertEquals("Bearer $TOKEN", observed!!.header("Authorization"))
        assertEquals("console-v1", observed!!.header("X-Console-Protocol"))
        session.close()
    }

    @Test
    fun crossOriginRequestStopsBeforeNetworkDispatch() {
        var dispatches = 0
        val client = clientWithResponse { request ->
            dispatches += 1
            response(request, 200, "{}")
        }
        val session = session(tokenProvider(), client)

        val error = assertThrows(GatewayTransportException::class.java) {
            session.execute(getRequest("https://other.example/console/v1/test"))
        }

        assertEquals(GatewayFailureReason.PROTOCOL_ERROR, error.reason)
        assertEquals(0, dispatches)
        session.close()
    }

    @Test
    fun oversizedResponseFailsAsProtocolError() {
        val oversized = "x".repeat(ConsoleWireV1.MAX_MESSAGE_BYTES + 1)
        val client = clientWithResponse { request -> response(request, 200, oversized) }
        val session = session(tokenProvider(), client)

        val error = assertThrows(GatewayTransportException::class.java) {
            session.execute(getRequest("https://gateway.example/console/v1/test"))
        }

        assertEquals(GatewayFailureReason.PROTOCOL_ERROR, error.reason)
        assertFalse(error.retryable)
        session.close()
    }

    @Test
    fun productionClientDisablesRedirectsRetriesAndLoggingInterceptors() {
        val client = GatewayOkHttpClientFactory.create(CONFIGURATION)

        assertFalse(client.followRedirects)
        assertFalse(client.followSslRedirects)
        assertFalse(client.retryOnConnectionFailure)
        assertTrue(client.interceptors.isEmpty())

        client.dispatcher.executorService.shutdown()
        client.connectionPool.evictAll()
    }

    @Test
    fun websocketHandshakeInjectsCredentialAndMapsAuthorizationFailure() {
        var observed: Request? = null
        val terminal = CountDownLatch(1)
        var failure: GatewayCallResult.Failure? = null
        val client = clientWithResponse { request ->
            observed = request
            response(request, 401, "")
        }
        val session = session(tokenProvider(), client)

        session.openEventStream(
            deviceId = "shaniu-test",
            generation = 7,
            afterSequence = 12,
            observer = object : GatewayEventObserver {
                override fun onOpen() = Unit
                override fun onEvent(event: com.shaniu.companion.protocol.ConsoleEventEnvelope) = Unit
                override fun onClosed() = Unit
                override fun onFailure(value: GatewayCallResult.Failure) {
                    failure = value
                    terminal.countDown()
                }
            },
        )

        assertTrue(terminal.await(2, TimeUnit.SECONDS))
        assertEquals("Bearer $TOKEN", observed!!.header("Authorization"))
        assertEquals(
            "https://gateway.example/console/v1/devices/shaniu-test/events" +
                "?generation=7&after_sequence=12",
            observed!!.url.toString(),
        )
        assertEquals(GatewayFailureReason.AUTHORIZATION_REVOKED, failure!!.reason)
        assertFalse(failure!!.retryable)
        session.close()
    }

    private fun session(
        provider: GatewayAccessTokenProvider,
        client: OkHttpClient,
    ) = OkHttpConsoleGatewaySession(CONFIGURATION, provider, client)

    private fun tokenProvider() = GatewayAccessTokenProvider { TOKEN }

    private fun getRequest(uri: String) = GatewayHttpRequest(
        method = GatewayHttpMethod.GET,
        uri = URI(uri),
        headers = mapOf(
            "Accept" to ConsoleGatewayClient.MEDIA_TYPE,
            "X-Console-Protocol" to "console-v1",
        ),
    )

    private fun clientWithResponse(block: (Request) -> Response): OkHttpClient =
        OkHttpClient.Builder()
            .addInterceptor(Interceptor { chain -> block(chain.request()) })
            .build()

    private fun response(request: Request, code: Int, body: String) = Response.Builder()
        .request(request)
        .protocol(Protocol.HTTP_1_1)
        .code(code)
        .message("test")
        .body(body.toResponseBody(ConsoleGatewayClient.MEDIA_TYPE.toMediaType()))
        .build()

    companion object {
        private const val TOKEN = "test-token-0123456789"
        private val CONFIGURATION = GatewayTransportConfiguration(
            GatewayOrigin.parse("https://gateway.example"),
        )
    }
}
