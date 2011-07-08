{- 
  Register table: list of all registers
   
  Part of Mackerel: a strawman device definition DSL for Barrelfish
   
  Copyright (c) 2007, 2008, ETH Zurich.
  All rights reserved.
  
  This file is distributed under the terms in the attached LICENSE file.
  If you do not find this file, copies can be found by writing to:
  ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
-}  

module RegisterTable where

import MackerelParser
import Text.ParserCombinators.Parsec
import Attr
import qualified Fields as F
import qualified TypeTable as TT
import qualified Space

{--------------------------------------------------------------------
Register table: list of all registers 
--------------------------------------------------------------------}

data Rec = Rec { name :: String,
                 fl :: [F.Rec],
                 tpe :: TT.Rec,
                 origtype :: String,
                 size :: Integer,
                 also :: Bool,
                 desc :: String,
                 spc_id :: String,
                 spc :: Space.Rec,
                 base :: String,
                 offset :: Integer,
                 arr :: ArrayLoc,
                 pos :: SourcePos
               }
         deriving Show

--
-- Building the register table
--
make_table :: [TT.Rec] -> [AST] -> BitOrder -> [Space.Rec] -> [Rec]
make_table rtinfo decls order spt = 
    concat [ (make_reginfo rtinfo d order spt) | d <- decls ]


make_regproto n als rloc dsc p spt t =
    let (si, s, b, o) = get_location rloc spt
    in
      Rec { name = n,
            fl = [],
            tpe = t,
            origtype = "",
            size = 0,
            also = als,
            desc = dsc, 
            spc_id = si,
            spc = s,
            base = b, 
            offset = o, 
            arr = (ArrayListLoc []),
            pos = p } 



make_reginfo :: [TT.Rec] -> AST -> BitOrder -> [Space.Rec] -> [Rec]

make_reginfo rtinfo (RegArray n attr als rloc aloc dsc (TypeDefn decls) p) order spt =
    let r = make_regproto n als rloc dsc p spt (TT.get_rtrec rtinfo n)
    in
      [ r { fl = F.make_list attr order 0 decls,
            size = TT.tt_size (TT.get_rtrec rtinfo n),
            arr = aloc } ]

make_reginfo rtinfo (RegArray n attr als rloc aloc dsc (TypeRef tname) p) order spt
    | TT.is_builtin_type tname = 
        let t = (TT.Primitive tname (TT.builtin_size tname) attr)
            r = make_regproto n als rloc dsc p spt t
        in [ r { fl = [],
                 origtype = tname,
                 size = (TT.tt_size t),
                 arr = aloc } ]
    | otherwise = 
        let rt = (TT.get_rtrec rtinfo tname)
            r = make_regproto n als rloc dsc p spt rt
        in
          [ r { fl = F.inherit_list attr (TT.fields rt),
                origtype = tname,
                size = (TT.tt_size rt),
                arr = aloc } ]

make_reginfo rtinfo (Register n attr als rloc dsc (TypeDefn decls) p) order spt =
    let r = make_regproto n als rloc dsc p spt (TT.get_rtrec rtinfo n)
    in
      [ r { fl = F.make_list attr order 0 decls,
            size = TT.tt_size (TT.get_rtrec rtinfo n),
            arr = (ArrayListLoc []) } ]

make_reginfo rtinfo (Register n attr als rloc dsc (TypeRef tname) p) order spt 
    | TT.is_builtin_type tname = 
        let t = (TT.Primitive tname (TT.builtin_size tname) attr)
            r = make_regproto n als rloc dsc p spt t
        in [ r { origtype = tname,
                 size = (TT.tt_size t),
                 arr = (ArrayListLoc []) } ]

    | otherwise = 
        let rt = (TT.get_rtrec rtinfo tname)
            r = make_regproto n als rloc dsc p spt rt
        in
          [ r { fl = F.inherit_list attr (TT.fields rt),
                origtype = tname,
                size = (TT.tt_size rt),
                arr = (ArrayListLoc []) } ]
          
make_reginfo rtinfo _ _ _ = []

get_location :: RegLoc -> [Space.Rec] -> ( String, Space.Rec, String, Integer )
get_location (RegLoc s b o) spt = 
    ( s, Space.lookup s spt, b, o)

overlap :: Rec -> Rec -> Bool
overlap r1 r2 
    | spc_id r1 /= spc_id r2 = False
    | base r1 /= base r2 = False
    | otherwise = 
        any extent_overlap [ (e1, e2) | e1 <- extents r1, e2 <- extents r2 ]

extents :: Rec -> [ (Integer, Integer) ]
extents r = [ ((offset r) + o, (extentsz (Space.t (spc r)) (size r)))
                  | o <- arrayoffsets (arr r) (size r)]
extentsz :: Space.SpaceType -> Integer -> Integer
extentsz (Space.BYTEWISE s) sz = sz `div` 8 `div` s
extentsz _ sz = 1

    

arrayoffsets :: ArrayLoc -> Integer -> [ Integer ]
arrayoffsets (ArrayListLoc []) _ = [0]
arrayoffsets (ArrayListLoc l) _ = l
arrayoffsets (ArrayStepLoc n 0) sz = enumFromThenTo 0 (sz `div` 8) (sz* (n-1) `div` 8)
arrayoffsets (ArrayStepLoc n s) _ = enumFromThenTo 0 s (s* (n-1))

extent_overlap :: ( (Integer, Integer),  (Integer, Integer)) -> Bool
extent_overlap ( (b1, o1), (b2, o2) )
    |  b1 > b2 = ( b2 + o2 > b1 )
    | otherwise = ( b1 + o1 > b2 )

--
-- Lookups
-- 
lookup_reg :: [Rec] -> String -> Rec 
lookup_reg reginfo n = 
    head l where l = [ r | r <- reginfo, (name r) == n ]

lookup_size :: [Rec] -> String -> Integer              
lookup_size reginfo n = (size (lookup_reg reginfo n ))

-- 
-- Properties of registers
--

is_writeable :: Rec -> Bool
is_writeable r = 
    case (tpe r) of
      (TT.Primitive _ _ attr) -> attr_is_writeable attr
      _ -> any F.is_writeable (fl r)

is_readable :: Rec -> Bool
is_readable r = 
    case (tpe r) of
      (TT.Primitive _ _ attr) -> attr_is_readable attr
      _ -> any F.is_readable (fl r)

is_writeonly :: Rec -> Bool
is_writeonly r = 
    case (tpe r) of
      (TT.Primitive _ _ attr) -> attr_is_writeonly attr
      _ -> any F.is_writeonly (fl r)

needs_shadow :: Rec -> Bool
needs_shadow r = is_writeonly r

typename :: Rec -> String
typename r = (TT.tt_name (tpe r))

is_array :: Rec -> Bool
is_array (Rec { arr = (ArrayListLoc []) } ) = False
is_array r = True

num_elements :: Rec -> Integer
num_elements Rec { arr = (ArrayListLoc l) } = toInteger (length l)
num_elements Rec { arr = (ArrayStepLoc num _) } = num

needs_read_before_write :: Rec -> Bool
needs_read_before_write r = any F.is_rsvd (fl r)

data Shadow = Shadow String String
--                   name   type
get_shadows :: [Rec] -> [Shadow]
get_shadows reginfo = 
    [ Shadow (name r) (TT.tt_name (tpe  r)) 
          | r <- reginfo, needs_shadow r ]

get_shadow_registers :: [Rec] -> [Rec]
get_shadow_registers reginfo = [ r | r <- reginfo, needs_shadow r ]
