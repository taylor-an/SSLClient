/* Copyright 2019 OSU OPEnS Lab
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this
 * software and associated documentation files (the "Software"), to deal in the Software
 * without restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <type_traits>
#include "Client.h"
#include "SSLClientImpl.h"
#include "SSLSession.h"

#ifndef SSLClient_H_
#define SSLClient_H_

/**
 * @brief The main SSLClient class.
 * Check out README.md for more info.
 */

template <class C, size_t SessionCache = 1>
class SSLClient : public SSLClientImpl {
/* 
 * static checks
 * I'm a java developer, so I want to ensure that my inheritance is safe.
 * These checks ensure that all the functions we use on class C are
 * actually present on class C. It does this by checking that the
 * class inherits from Client.
 * 
 * Additionally, I ran into a lot of memory issues with large sessions caches.
 * Since each session contains at max 352 bytes of memory, they eat of the
 * stack quite quickly and can cause overflows. As a result, I have added a
 * warning here to discourage the use of more than 3 sessions at a time. Any
 * amount past that will require special modification of this library, and 
 * assumes you know what you are doing.
 */
static_assert(std::is_base_of<Client, C>::value, "SSLClient can only accept a type with base class Client!");
static_assert(SessionCache > 0 && SessionCache < 255, "There can be no less than one and no more than 255 sessions in the cache!");
static_assert(SessionCache <= 3, "You need to decrease the size of m_iobuf in order to have more than 3 sessions at once, otherwise memory issues will occur.");

public:
    /**
     * @brief Initialize SSLClient with all of the prerequisites needed.
     * 
     * @pre You will need to generate an array of trust_anchors (root certificates)
     * based off of the domains you want to make SSL connections to. Check out the
     * TrustAnchors.md file for more info.
     * @pre The analog_pin should be set to input.
     * 
     * @param client The base network device to create an SSL socket on. This object will be copied
     * and the copy will be stored in SSLClient.
     * @param trust_anchors Trust anchors used in the verification 
     * of the SSL server certificate. Check out TrustAnchors.md for more info.
     * @param trust_anchors_num The number of objects in the trust_anchors array.
     * @param analog_pin An analog pin to pull random bytes from, used in seeding the RNG.
     * @param debug The level of debug logging (use the ::DebugLevel enum).
     */
    explicit SSLClient(const C& client, const br_x509_trust_anchor *trust_anchors, const size_t trust_anchors_num, const int analog_pin, const DebugLevel debug = SSL_WARN)
    : SSLClientImpl(trust_anchors, trust_anchors_num, analog_pin, debug) 
    , m_client(client)
    , m_sessions{SSLSession()}
    {
        // set the timeout to a reasonable number (it can always be changes later)
        // SSL Connections take a really long time so we don't want to time out a legitimate thing
        setTimeout(10 * 1000);
    }
    
    //========================================
    //= Functions implemented in SSLClientImpl
    //========================================

    /**
     * @brief Connect over SSL to a host specified by an IP address.
     * 
     * SSLClient::connect(host, port) should be preferred over this function, 
     * as verifying the domain name is a step in ensuring the certificate is 
     * legitimate, which is important to the security of the device. Additionally,
     * SSL sessions cannot be resumed when using this function, which can drastically increase initial
     * connect time.
     * 
     * This function initializes the socket by calling m_client::connect(IPAddress, uint16_t)
     * with the parameters supplied, then once the socket is open, uses BearSSL to
     * to complete a SSL handshake. Due to the design of the SSL standard, 
     * this function will probably take an extended period (1-4sec) to negotiate 
     * the handshake and finish the connection. This function runs until the SSL 
     * handshake succeeds or fails.
     * 
     * SSL requires the client to generate some random bits (to be later combined 
     * with some random bits from the server), so SSLClient uses the least significant 
     * bits from the analog pin supplied in the constructor. The random bits are generated
     * from 16 consecutive analogReads, and given to BearSSL before the handshake
     * starts.
     * 
     * The implementation for this function can be found in SSLClientImpl::connect_impl(IPAddress, uint16_t).
     * 
     * @pre The underlying client object (passed in through the constructor) is in a non-
     * error state, and must be able to access the IP.
     * @pre SSLClient can only have one connection at a time, so the client
     * object must not already be connected.
     * @pre There must be sufficient memory available on the device to verify
     * the certificate (if the free memory drops below 8000 bytes during certain
     * points in the connection, SSLClient will fail).
     * @pre There must be a trust anchor given to the constructor that corresponds to
     * the certificate provided by the IP address being connected to. For more
     * information check out TrustAnchors.md .
     * 
     * @param ip The IP address to connect to
     * @param port the port to connect to
     * @returns 1 if success, 0 if failure
     */
    virtual int connect(IPAddress ip, uint16_t port) { return connect_impl(ip, port); }

    /**
     * @brief Connect over SSL to a host specified by a hostname.
     * 
     * This function initializes the socket by calling m_client::connect(const char*, uint16_t)
     * with the parameters supplied, then once the socket is open, uses BearSSL to
     * complete a SSL handshake. This function runs until the SSL handshake 
     * succeeds or fails.
     * 
     * SSL requires the client to generate some random bits (to be later combined 
     * with some random bits from the server), so SSLClient uses the least significant 
     * bits from the analog pin supplied in the constructor. The random bits are generated
     * from 16 consecutive analogReads, and given to BearSSL before the handshake
     * starts.
     * 
     * This function will usually take around 4-10 seconds. If possible, this function
     * also attempts to resume the SSL session if one is present matching the hostname 
     * string, which will reduce connection time to 100-500ms. To read more about this 
     * functionality, check out Session Caching in the README.
     * 
     * The implementation for this function can be found in SSLClientImpl::connect_impl(const char*, uint16_t)
     * 
    * @pre The underlying client object (passed in through the constructor) is in a non-
     * error state, and must be able to access the IP.
     * @pre SSLClient can only have one connection at a time, so the client
     * object must not already be connected.
     * @pre There must be sufficient memory available on the device to verify
     * the certificate (if the free memory drops below 8000 bytes during certain
     * points in the connection, SSLClient will fail).
     * @pre There must be a trust anchor given to the constructor that corresponds to
     * the certificate provided by the IP address being connected to. For more
     * information check out TrustAnchors.md .
     * 
     * @param host The hostname as a null-terminated c-string ("www.google.com")
     * @param port The port to connect to on the host (443 for HTTPS)
     * @returns 1 of success, 0 if failure
     */
	virtual int connect(const char *host, uint16_t port) { return connect_impl(host, port); }

    /** @see SSLClient::write(uint8_t*, size_t) */
    virtual size_t write(uint8_t b) { return write_impl(&b, 1); }
    /**
     * @brief Write some bytes to the SSL connection
     * 
     * Assuming all preconditions are met, this function writes data to the BearSSL IO 
     * buffer, BUT does not initially send the data. Instead, you must call 
     * SSLClient::available or SSLClient::flush, which will detect that 
     * the buffer is ready for writing, and will write the data to the network. 
     * Alternatively, if this function is requested to write a larger amount of data than SSLClientImpl::m_iobuf
     * can handle, data will be written to the network in pages the size of SSLClientImpl::m_iobuf until
     * all the data in buf is sent--attempting to keep all writes to the network grouped together. For information
     * on why this is the case check out README.md .
     * 
     * The implementation for this function can be found in SSLClientImpl::write_impl(const uint8_t*, size_t)
     * 
     * @pre The socket and SSL layer must be connected, meaning SSLClient::connected must be true.
     * @pre BearSSL must not be waiting for the recipt of user data (if it is, there is
     * probably an error with how the protocol in implemented in your code).
     * 
     * @param buf the pointer to a buffer of bytes to copy
     * @param size the number of bytes to copy from the buffer
     * @returns The number of bytes copied to the buffer (size), or zero if the BearSSL engine 
     * fails to become ready for writing data.
     */
	virtual size_t write(const uint8_t *buf, size_t size) { return write_impl(buf, size); }

    /**
     * @brief Returns the number of bytes available to read from the data that has been received and decrypted.
     * 
     * This function updates the state of the SSL engine (including writing any data, 
     * see SSLClient::write) and as a result should be called periodically when expecting data. 
     * Additionally, since if there are no bytes and if SSLClient::connected is false 
     * this function returns zero (this same behavior is found
     * in EthernetClient), it is prudent to ensure in your own code that the 
     * preconditions are met before checking this function to prevent an ambiguous
     * result.
     * 
     * The implementation for this function can be found in SSLClientImpl::available
     * 
     * @pre SSLClient::connected must be true.
     * 
     * @returns The number of bytes available (can be zero), or zero if any of the pre
     * conditions aren't satisfied.
     */
	virtual int available() { return available_impl(); }

    /** 
     * @brief Read a single byte, or -1 if none is available.
     * @see SSLClient::read(uint8_t*, size_t) 
     */
	virtual int read() { uint8_t read_val; return read(&read_val, 1) > 0 ? read_val : -1; };
    /**
     * @brief Read size bytes from the SSL client buffer, copying them into *buf, and return the number of bytes read.
     * 
     * This function checks if bytes are ready to be read by calling SSLClient::available,
     * and if so copies size number of bytes from the IO buffer into the buf pointer.
     * Data read using this function will not 
     * include any SSL or socket commands, as the Client and BearSSL will capture those and 
     * process them separately.
     * 
     * If you find that you are having a lot of timeout errors, SSLClient may be experiencing a buffer
     * overflow. Checkout README.md for more information.
     * 
     * The implementation for this function can be found in SSLClientImpl::read_impl(uint8_t*, size_t)
     * 
     * @pre SSLClient::available must be >0
     * 
     * @param buf The pointer to the buffer to put SSL application data into
     * @param size The size (in bytes) to copy to the buffer
     * 
     * @returns The number of bytes copied (<= size), or -1 if the preconditions are not satisfied.
     */
	virtual int read(uint8_t *buf, size_t size) { return read_impl(buf, size); }

    /** 
     * @brief View the first byte of the buffer, without removing it from the SSLClient Buffer
     * 
     * The implementation for this function can be found in SSLClientImpl::peek 
     * @pre SSLClient::available must be >0
     * @returns The first byte received, or -1 if the preconditions are not satisfied (warning: 
     * do not use if your data may be -1, as the return value is ambiguous)
     */
    virtual int peek() { return peek_impl(); }

    /**
     * @brief Force writing the buffered bytes from SSLClient::write to the network.
     * 
     * This function is blocking until all bytes from the buffer are written. For
     * an explanation of how writing with SSLClient works, please see SSLClient::write.
     * The implementation for this function can be found in SSLClientImpl::flush.
     */
	virtual void flush() { return flush_impl(); }

    /**
     * @brief Close the connection
     *
     * If the SSL session is still active, all incoming data is discarded and BearSSL will attempt to
     * close the session gracefully (will write to the network), and then call m_client::stop. If the session is not active or an
     * error was encountered previously, this function will simply call m_client::stop.
     * The implementation for this function can be found in SSLClientImpl::peek.
     */
	virtual void stop() { return stop_impl(); }

    /**
     * @brief Check if the device is connected.
     *
     * Use this function to determine if SSLClient is still connected and a SSL connection is active.
     * It should be noted that SSLClient::available should be preferred over this function for rapid
     * polling--both functions send and receive data with the SSLClient::m_client device, however SSLClient::available
     * has some delays built in to protect SSLClient::m_client from being polled too frequently. 
     * 
     * The implementation for this function can be found in SSLClientImpl::connected_impl.
     * 
     * @returns 1 if connected, 0 if not
     */
	virtual uint8_t connected() { return connected_impl(); }

    //========================================
    //= Functions Not in the Client Interface
    //========================================

    /**
     * @brief Gets a session reference corresponding to a host and IP, or a reference to a empty session if none exist
     * 
     * If no session corresponding to the host and IP exist, then this function will cycle through
     * sessions in a rotating order. This allows the session cache to continually store sessions,
     * however it will also result in old sessions being cleared and returned. In general, it is a
     * good idea to use a SessionCache size equal to the number of domains you plan on connecting to.
     * 
     * The implementation for this function can be found at SSLClientImpl::get_session_impl.
     * 
     * @param host A hostname c string, or NULL if one is not available
     * @param addr An IP address
     * @returns A reference to an SSLSession object
     */
    virtual SSLSession& getSession(const char* host, const IPAddress& addr) { return get_session_impl(host, addr); }

    /**
     * @brief Clear the session corresponding to a host and IP
     * 
     * The implementation for this function can be found at SSLClientImpl::remove_session_impl.
     * 
     * @param host A hostname c string, or NULL if one is not available
     * @param addr An IP address
     */
    virtual void removeSession(const char* host, const IPAddress& addr) { return remove_session_impl(host, addr); }

    /**
     * @brief Get the maximum number of SSL sessions that can be stored at once
     *
     *  @returns The SessionCache template parameter.
     */
    virtual size_t getSessionCount() const { return SessionCache; }

    /** 
     * @brief Equivalent to SSLClient::connected() > 0
     * 
     * @returns true if connected, false if not
     */
	virtual operator bool() { return connected() > 0; }
    /** @see SSLClient::operator bool */
	virtual bool operator==(const bool value) { return bool() == value; }
    /** @see SSLClient::operator bool */
	virtual bool operator!=(const bool value) { return bool() != value; }
    /** @brief Returns whether or not two SSLClient objects have the same underlying client object */
	virtual bool operator==(const C& rhs) { return m_client == rhs; }
    /** @brief Returns whether or not two SSLClient objects do not have the same underlying client object */
	virtual bool operator!=(const C& rhs) { return m_client != rhs; }
    /** @brief Returns the local port, C::localPort exists. Else return 0. */
	virtual uint16_t localPort() {
        if (std::is_member_function_pointer<decltype(&C::localPort)>::value) return m_client.localPort();
        else {
            m_warn("Client class has no localPort function, so localPort() will always return 0", __func__);
            return 0;
        } 
    }
    /** @brief Returns the remote IP, if C::remoteIP exists. Else return INADDR_NONE. */
	virtual IPAddress remoteIP() { 
        if (std::is_member_function_pointer<decltype(&C::remoteIP)>::value) return m_client.remoteIP();
        else {
            m_warn("Client class has no remoteIP function, so remoteIP() will always return INADDR_NONE. This means that sessions caching will always be disabled.", __func__);
            return INADDR_NONE;
        } 
    }
    /** @brief Returns the remote port, if C::remotePort exists. Else return 0. */
	virtual uint16_t remotePort() {
        if (std::is_member_function_pointer<decltype(&C::remotePort)>::value) return m_client.remotePort();
        else {
            m_warn("Client class has no remotePort function, so remotePort() will always return 0", __func__);
            return 0;
        } 
    }

    /** @brief Returns a reference to the client object stored in this class. Take care not to break it. */
    C& getClient() { return m_client; }

protected:
    /** @brief Returns an instance of m_client that is polymorphic and can be used by SSLClientImpl */
    virtual Client& get_arduino_client() { return m_client; }
    virtual const Client& get_arduino_client() const { return m_client; }
    /** @brief Returns an instance of the session array that is on the stack */
    virtual SSLSession* get_session_array() { return m_sessions; }
    virtual const SSLSession* get_session_array() const { return m_sessions; }

private:
    // create a copy of the client
    C m_client;
    // also store an array of SSLSessions, so we can resume communication with multiple websites
    SSLSession m_sessions[SessionCache];
};

#endif /** SSLClient_H_ */