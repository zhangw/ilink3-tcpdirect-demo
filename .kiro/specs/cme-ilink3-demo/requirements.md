# Requirements Document

## Introduction

This document specifies the requirements for a CME iLink3 protocol demo application. The application will demonstrate the core functionality of the iLink3 protocol version 9, including connection establishment, heartbeat message handling, and connection termination. The implementation will use TCPDirect (zf_* APIs) for high-performance TCP communication with Solarflare NICs.

## Glossary

- **CME iLink3**: CME Group's high-performance market data feed handler protocol
- **MSGW**: Market Segment Gateway - CME's high-performance order gateway
- **TCPDirect**: High-performance TCP/IP stack library (zf_* APIs) for Solarflare NICs
- **Heartbeat**: Periodic message exchanged between client and server to maintain connection validity
- **Session**: A logical connection between client and CME MSGW
- **Solarflare NIC**: High-performance network interface card with TCPDirect support

## Requirements

### Requirement 1: Protocol Version Configuration

**User Story:** Only for iLink3 protocol version 9.

#### Acceptance Criteria

1. THE Application SHALL support iLink3 protocol version 9

### Requirement 2: Network Configuration

**User Story:** As a developer, I want to configure network parameters, so that I can connect to CME MSGW or simulate a server.

#### Acceptance Criteria

1. Only as the client, not server
4. If invalid network configuration is provided, the application shall return a descriptive error

### Requirement 3: TCP Connection Establishment

**User Story:** As a user, I want to establish a TCP connection to CME MSGW, so that I can begin iLink3 session negotiation.

#### Acceptance Criteria

1. The Application shall initiate a TCP connection to the configured CME MSGW IP address and port
2. WHEN the TCP connection is successfully established, THE Application SHALL transition to iLink3 session establishment
3. IF the TCP connection fails, THEN THE Application SHALL report the connection failure with error details

### Requirement 4: iLink3 Session Establishment

**User Story:** As a user, I want to perform iLink3 session establishment, so that I can authenticate and configure the session with CME MSGW.

#### Acceptance Criteria

1. WHEN the TCP connection is established, THE Application SHALL send an iLink3 session establishment request
2. WHEN a valid iLink3 session establishment response is received, THE Application SHALL confirm session establishment
3. IF an invalid or rejected session establishment response is received, THEN THE Application SHALL report the failure
4. IF session establishment times out, THEN THE Application SHALL close the TCP connection

### Requirement 5: Heartbeat Message Request

**User Story:** As a user, I want to send heartbeat messages, so that I can maintain connection validity with CME MSGW.

#### Acceptance Criteria

1. WHEN the heartbeat interval expires, THE Application SHALL send a heartbeat request message
2. THE Application SHALL include the current timestamp in heartbeat messages
3. WHERE heartbeat acknowledgment is expected, THE Application SHALL wait for the response within the configured timeout
4. IF a heartbeat response is not received within the timeout, THEN THE Application SHALL log the missed heartbeat

### Requirement 6: Heartbeat Message Response

**User Story:** As a user, I want to respond to heartbeat messages from CME MSGW, so that I can maintain connection validity.

#### Acceptance Criteria

1. WHEN a heartbeat request message is received, THE Application SHALL process the message and send a heartbeat response
2. THE Application SHALL include the received timestamp in the heartbeat response
3. THE Application SHALL send the heartbeat response within the configured response time limit
4. IF processing a heartbeat request fails, THEN THE Application SHALL log the error and continue operation

### Requirement 7: Connection Termination

**User Story:** As a user, I want to gracefully terminate the connection, so that I can cleanly end the iLink3 session.

#### Acceptance Criteria

1. WHEN a termination request is initiated, THE Application SHALL send a graceful TCP shutdown
2. WHEN a graceful shutdown is received from the peer, THE Application SHALL complete the termination sequence
3. IF the termination sequence times out, THEN THE Application SHALL force close the connection
4. WHEN the connection is fully terminated, THE Application SHALL release all associated resources

### Requirement 8: Error Handling

**User Story:** As a user, I want proper error handling, so that I can diagnose issues with the iLink3 connection.

#### Acceptance Criteria

1. IF a network error occurs, THEN THE Application SHALL log the error with sufficient detail for diagnosis
2. IF an iLink3 protocol violation is detected, THEN THE Application SHALL log the violation and terminate the connection
3. IF resource allocation fails, THEN THE Application SHALL report the failure gracefully
4. THE Application SHALL provide error codes for different failure scenarios

### Requirement 9: Resource Management

**User Story:** As a developer, I want proper resource management, so that the application runs efficiently without leaks.

#### Acceptance Criteria

1. WHEN a TCP connection is established, THE Application SHALL allocate necessary TCPDirect resources
2. WHEN a connection is terminated, THE Application SHALL release all associated TCPDirect resources
3. WHEN the application exits, THE Application SHALL ensure all resources are properly freed
4. THE Application SHALL handle resource exhaustion gracefully

### Requirement 10: Demo Functionality

**User Story:** As a developer, I want clear demo output, so that I can verify the application is working correctly.

#### Acceptance Criteria

1. WHEN a connection is established, THE Application SHALL log the connection event
2. WHEN heartbeat messages are sent or received, THE Application SHALL log the message details
3. WHEN the connection is terminated, THE Application SHALL log the termination event
4. WHERE errors occur, THE Application SHALL log error messages with diagnostic information

## Testing Requirements

### Requirement T1: Manual Testing

**User Story:** As a user, I want manual testing capabilities, so that I can verify the application with CME MSGW.

#### Acceptance Criteria

1. THE Application SHALL provide command-line options for network settings, iLink3 auth settings
2. THE Application SHALL provide verbose logging for debugging
3. THE Application SHALL provide a simple way to trigger connection termination