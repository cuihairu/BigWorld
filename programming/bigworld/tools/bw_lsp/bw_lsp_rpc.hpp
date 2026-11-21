// bw_lsp_rpc.hpp
//
// The stdio transport between the extension and this engine binary. Frames
// follow the LSP base protocol (Content-Length headers) so a stock language
// client could talk to it too; the methods themselves are this tool's own bw/*
// namespace.

#ifndef BW_LSP_RPC_HPP
#define BW_LSP_RPC_HPP

#include "cstdmf/bw_namespace.hpp"
#include "cstdmf/bw_string.hpp"

#include <cstdio>

BW_BEGIN_NAMESPACE

class BwLspSession;

/**
 *	Reads one Content-Length framed body from in. Returns false on EOF or a
 *	malformed header.
 */
bool bwLspReadFrame( FILE * in, BW::string & body );

/**
 *	Writes one Content-Length framed body to out.
 */
bool bwLspWriteFrame( FILE * out, const BW::string & body );

/**
 *	Dispatches one JSON-RPC message to the session and returns the reply
 *	(empty for a notification). Returns false when the message asks the
 *	process to exit.
 */
bool bwLspHandleMessage( BwLspSession & session, const BW::string & messageBody,
	BW::string & responseBody );

/**
 *	The stdio server loop: reads requests, writes replies, until exit.
 */
void bwLspServe( BwLspSession & session );

BW_END_NAMESPACE

#endif // BW_LSP_RPC_HPP
