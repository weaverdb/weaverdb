#
# lex.sed - sed rules to remove conflicts between the 
#               bootstrap backend interface LEX scanner and the
#               normal backend SQL LEX scanner
#
# $Header: /cvs/weaver/mtpgsql/src/backend/bootstrap/boot.sed,v 1.1.1.1 2006/08/12 00:20:07 synmscott Exp $
#
s/^yy/Int_yy/g
s/\([^a-zA-Z0-9_]\)yy/\1Int_yy/g
