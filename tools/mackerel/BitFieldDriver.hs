{- 
  BitFieldDriver: Mackerel backend for device drivers using bitfields

  This driver is deprecated: please use the ShiftDriver (and
  associated different language bindings) instead.  Functionality of
  this driver across different compiler revisions and/or processor
  architectures is not guaranteed (or, indeed, likely).
   
  Part of Mackerel: a strawman device definition DSL for Barrelfish
   
  Copyright (c) 2007-2010, ETH Zurich.
  All rights reserved.
  
  This file is distributed under the terms in the attached LICENSE file.
  If you do not find this file, copies can be found by writing to:
  ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
-}  

module BitFieldDriver where

import System.IO
import System.Exit
import Data.List
import Text.Printf
import MackerelParser
import Checks

import Attr
import Space
import qualified CSyntax as C
import qualified TypeTable as TT
import qualified RegisterTable as RT
import qualified ConstTable as CT
import qualified Fields
import qualified Dev
import qualified Data.Maybe

dev_t = "__DN(t)"
dev_init = "__DN(initialize)"
dev_pr = "__DP(pr)"
dev_reg_ptr n = n ++ " *"
dev_ptr = dev_t ++ " *"

reg_nm n         = "__DP(" ++ n ++ ")"      -- Name of a register 
reg_wr n         = (reg_nm (n ++ "_wr"))    -- Fn to write a register
field_wr n f     = (reg_nm (n ++ "_" ++ f ++ "_wrf"))    -- Fn to write a register
reg_wr_raw n     = (reg_nm (n ++ "_wr_raw"))
reg_init n       = (reg_nm (n ++ "_init"))
reg_rd n         = (reg_nm (n ++ "_rd"))    -- Fn to read a register
reg_rd_raw n     = (reg_nm (n ++ "_rd_raw"))    -- Fn to read a raw register
reg_rds n        = (reg_nm (n ++ "_rd_shadow")) -- Read a register's shadow
reg_pv n         = (reg_nm (n ++ "_prtval"))    -- Fn to print a regtype type
reg_pr n         = (reg_nm (n ++ "_pr"))    -- Fn to print a register contents
reg_pri n         = (reg_nm (n ++ "_pri"))    -- Fn to print an array element
reg_chk n        = (reg_nm (n ++ "_chk"))   -- Fn to check data type to be written on that register
reg_fd n         = (reg_nm (n ++ "_fd"))   -- field read access
reg_addr n       = (reg_nm (n ++ "_addr"))
reg_len n       = (reg_nm (n ++ "_length"))

reg_t  n = (reg_nm (n ++ "_t"))         -- Name of a register type
reg_un :: String -> String
reg_un  n = (reg_nm (n ++ "_un"))         -- Name of a union type

enum_nm n = "__DP(" ++ n ++ ")"
enum_t n = (enum_nm (n ++ "_t"))
enum_pr n = (enum_nm (n ++ "_prt"))
enum_chk n = (enum_nm (n ++ "_chk"))

shadow nm = nm ++ "_shadow"

--
-- Rendering of Mackerel-specific constructs
-- 

-------------------------------------------------------------------------
-- Top-level header file rendering code
-------------------------------------------------------------------------

r_preamble dev = 
    "/*\n * DEVICE DEFINITION: " ++ (Dev.desc dev) ++ "\n\
    \ * \n\
    \ * Copyright (c) 2007, ETH Zurich.\n\
    \ * All rights reserved.\n\
    \ * \n\
    \ * This file is distributed under the terms in the attached LICENSE\n\
    \ * file. If you do not find this file, copies can be found by\n\
    \ * writing to:\n\
    \ * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich.\n\
    \ *  Attn: Systems Group.\n\
    \ * \n\
    \ * THIS FILE IS AUTOMATICALLY GENERATED: DO NOT EDIT!\n\
    \ */\n\n"

-- Top-level create-a-header-file
compile infile outfile dev =
    let dd = device_def dev ""
    in (r_preamble dev) ++ (C.header_file (Dev.name dev) dd)

render dev header = 
    let dd = device_def dev header
    in
      output ((r_preamble dev) ++ C.header_file (Dev.name dev) dd) 0

_success = exitWith ExitSuccess
_fail = exitWith (ExitFailure 1)
                            
output s exit = if(exit == 0) then 
                    (hPutStrLn stdout s) >> _success
                else
                    (hPutStrLn stderr s) >> _fail 

-- translation function mapped to every argument, for generic conversion
-- (eg. types or names) before rendering those arguments in C code

convert_arg (Arg "addr" x) = Arg "mackerel_addr_t" x
convert_arg (Arg "pci" x) = Arg "mackerel_pci_t" x
convert_arg (Arg "io" x) = Arg "mackerel_io_t" x

-- Body of the generated file
device_def dev header = 
    unlines ( std_header_files ++
              (device_prefix_defs (Dev.name dev)) ++   -- Macros
              (concat [ constants_decl d | d <- (Dev.cnstnts dev) ]) ++
              (concat [ regtype_decl d | d <- (Dev.types dev) ]) ++
              (concat [ datatype_decl d | d <- (Dev.dtypes dev) ]) ++
              (device_struct_def dev) ++
              (device_init_def dev) ++
              (space_includes dev header) ++
              (concat [ register_decl d | d <- (Dev.registers dev) ] ) ++
              (device_print dev) ++
              (device_prefix_undefs (Dev.name dev))   -- Undefine macros
              )

-- Undefine macros used by the header file
device_prefix_undefs :: String -> [String]
device_prefix_undefs name = 
    [ (C.undef ("__" ++ n)) | n <- [ "DN", "DP", "DP1", "DP2", "STR", "XTR" ] ] 

-- Define macros used by the header file
device_prefix_defs :: String -> [String]
device_prefix_defs name = 
    (device_prefix_undefs name)
    ++
    [ printf "#define __DN(x) %s ## _ ## x" name,
      printf "#ifdef %s_PREFIX" name,
      printf "#define __DP(x) __DP1(x,%s_PREFIX)" name,
      "#define __DP1(x1,x2) __DP2(x1,x2)",
      "#define __DP2(x1,x2) x2 ## x1",
      "#else",
      printf "#define __DP(x) %s##_ ##x" name,
      "#endif",
      "#define __STR(x) #x",
      "#define __XTR(x) __STR(x)"
    ]

-- Header files info
std_header_files = [ "#include <mackerel/mackerel.h>", "#include <inttypes.h>" ]
             
-- Device representation structure generator              
device_struct_def :: Dev.Rec -> [String]
device_struct_def d =
    [ C.multi_comment "Device representation structure",
      C.typedef dev_t (unlines strct) ]
    where
      args = map convert_arg (Dev.args d)
      strct = C.struct dev_t ( [ C.comment "Device arguments" ]
                               ++
                               [ (C.struct_field n v) | Arg n v <- args ]
                               ++
                               [ C.comment "Shadow registers" ]
                               ++
                               [ device_struct_shadow s | s <- (Dev.shdws d)]
                               )
    
device_struct_shadow (RT.Shadow n t) 
    | TT.is_builtin_type t = C.struct_field (builtin_to_c t) (shadow n)
    | otherwise = C.struct_field (reg_un t) (shadow n)

device_init_shadow (RT.Shadow n t)
    | TT.is_builtin_type t = printf "_dev->%s = %s;" (shadow n) "0x0"
    | otherwise = printf "_dev->%s.raw = %s;" (shadow n) "0x0" 

-- Device init function 
device_init_def :: Dev.Rec -> [String]
device_init_def d = 
    let tn = reg_t (Dev.name d)
        args = map convert_arg (Dev.args d)
    in
      [ C.multi_comment "Device Initialization function\n",
        C.inline "void" dev_init 
             ([(dev_ptr,"_dev")] ++ [ (n,v) | (Arg n v) <- args ] )
             ([ "/* Setting up device arguments*/" ] 
              ++   
              [ printf "_dev->%s = %s;" v v | (Arg n v)<- args ] 
              ++
              [ "/* Setting up shadow registers*/" ] 
              ++
              [ device_init_shadow s | s <- (Dev.shdws d)]
             )
      ]

device_print :: Dev.Rec -> [String]
device_print d = 
    [ C.inline "int" dev_pr
                   ( [ ("char *","s"), ("size_t","sz"), (dev_t, "* _dev") ] )
                   ( ["int r=0;",
                      "int _avail, _rc;" ] ++
                     (C.snputsq "-------------------------\\n") ++
                     (C.snputsq (printf "Dump of device %s (%s):\\n" (Dev.name d) (percent_escape (Dev.desc d)))) ++ 
                     (concat [ device_print_eachreg r | r <- (Dev.registers d)]) ++
                     (C.snputsq (printf "End of dump of device %s\\n" (Dev.name d))) ++
                     (C.snputsq "-------------------------\\n") ++
                     [ "return r;" ]
                   ) ]

device_print_eachreg reg = 
    C.snlike (reg_pr (RT.name reg)) "_dev"

space_includes d header
    | header /= "" =
        [ C.comment "Space access include file overridden by cmd line:",
          C.include_local header ]
    | all Space.is_builtin (Dev.spaces d) = 
        [ C.comment "No user-defined spaces" ]
    | otherwise = 
        [ C.comment "Include access functions for user-defined spaces",
          C.include_local $ printf "%s_spaces" (Dev.name d) ]

-------------------------------------------------------------------------
-- Render 'constants' declarations
-------------------------------------------------------------------------

constants_decl :: CT.Rec -> [String]
constants_decl c = [ (constants_comment c),
                  (constants_typedef c),
                  (constants_print_fn c),
                  (constants_check_fn c) ]
      
constants_comment c = 
    C.multi_comment (printf "Constant definition: %s (%s)" (CT.name c) (CT.desc c))
    
constants_typedef c = 
    C.enum (enum_t (CT.name c)) [ (enum_nm(CT.cname v), 
                                   (C.expression (CT.cval v)) )
                                  | v <- (CT.vals c) ]

constants_print_fn c = 
    let etype = enum_t (CT.name c)
    in
      C.inline "int" 
           (enum_pr (CT.name c))
           [ ("char *","s"), ("size_t","sz"), (etype,"e") ]  
            (constants_print_body etype (CT.vals c))


constants_print_body etype vals = 
    C.switch "e" 
         [ ( (enum_nm (CT.cname v)), 
             printf "return snprintf(s, sz, \"%%s\", \"%s\");" (CT.cdesc v) )
               | v <- vals ]
         (printf "return snprintf(s, sz, \"Unknown \" __XTR(%s) \" value 0x%%\" PRIxPTR, (uintptr_t)e);" etype )

constants_check_fn c =
    let etype = enum_t (CT.name c)
    in
      C.inline "int" 
           (enum_chk (CT.name c))
            [(etype,"e") ] 
            (constants_check_body etype (CT.vals c))
    
constants_check_body etype vals =  
    C.switch "e"
     [ ((enum_nm (CT.cname v)), "return 1;") | v <- vals ]
     "return 0;" 

-------------------------------------------------------------------------
-- Render 'register type definitions
-------------------------------------------------------------------------

builtin_to_c t = (t ++ "_t")

round_field_size w 
    | w <= 8 =                  "uint8_t" 
    | ( w > 8 && w <= 16 ) =    "uint16_t" 
    | ( w > 16 && w <= 32 ) =   "uint32_t" 
    | (w > 32 && w <= 64) =     "uint64_t"      

regtype_decl (TT.Format tname size td desc _ _) =
    let rtype = reg_t tname
        rname = reg_nm tname
        rtype_ptr = dev_reg_ptr rtype 
        sz = round_field_size size
    in
      [ (C.multi_comment ("Register type: " ++ desc )),
        (regtype_dump rtype td),
        (regtype_struct rtype td),
        (regtype_assert rtype td sz),
        (regtype_union  tname td sz),
        (regtype_print_fn tname rtype td rname)
      ] 


regtype_dump rtype td =
    C.multi_comment ( "Dump of fields for register type: " ++ rtype ++ "\n" ++ 
                      unlines([ field_dump f | f <- td ] ))

-- The type (bitfield) definition of the register type
regtype_struct rtype td = 
    C.packed_typedef rtype (unlines ( C.bitfields rtype (fields td) ))

regtype_assert rtype td sz =
    C.assertsize rtype sz

-- A union comprising the bitfield and a similarly-sized integer type
regtype_union tn td sz = 
    let un = (reg_un tn)
    in
      C.typedef un (unlines ( C.union un [C.union_field (reg_t tn) "val",
                                          C.union_field sz "raw"
                                        ] ))

fields td = 
    [ C.bitfield (Fields.name f) (Fields.size f) (field_type f) | f <- td ]

field_type f 
    | Fields.tpe f == Nothing = round_field_size $ Fields.size f
    | otherwise = reg_t $ Data.Maybe.fromJust $ Fields.tpe f

-- Print out a value of the register type
regtype_print_fn :: String -> String -> [Fields.Rec] -> String -> String
regtype_print_fn tn rtype td rname = 
    C.inline "int" (reg_pv tn) 
         ( [ ("char *","s"), ("size_t","sz"), (rtype, "v") ] )
         ( ["int r=0;",
            "int _avail, _rc;" ] ++
           concat [ field_print f  | f <- td, not $ Fields.is_anon f ] ++
           ["return r;" ]
         )

field_print :: Fields.Rec -> [String]
field_print f = 
    case Fields.tpe f of
      Nothing -> 
          C.snprintf (printf "\" %s=0x%s (%s)\\n\", (%s)(v.%s)"  
                             (Fields.name  f)
                       (field_fmt_str $ Fields.size f)
                       (percent_escape $ Fields.desc f)
                       (round_field_size $ Fields.size f) 
                       (Fields.name f) )
      Just t -> 
          (C.snputsq (printf " %s=" (Fields.name f))) 
          ++
          (C.snlike (enum_pr t) ("v." ++ (Fields.name f)))
          ++
          (C.snputsq (printf " (%s)\\n" (Fields.desc f)))
    
field_fmt_str size 
    | size <= 8 = "%\"PRIx8\""
    | size <= 16 = "%0\"PRIx16\""
    | size <= 32 = "%0\"PRIx32\""
    | otherwise = "%0\"PRIx64\""

percent_escape :: String -> String
percent_escape s = concat [ if c == '%' then "%%" else [c] | c <- s ]

-------------------------------------------------------------------------
-- Render data type definitions
-------------------------------------------------------------------------

datatype_decl (TT.Format tname size td desc _ _) =
    let rtype = reg_t tname
        rname = reg_nm tname
        rtype_ptr = dev_reg_ptr rtype 
        sz = round_field_size size
    in
      [ (C.multi_comment ("Data type: " ++ desc )),
        (regtype_struct rtype td),
        (regtype_print_fn tname rtype td rname)
      ] 

-------------------------------------------------------------------------
-- Render register definitions
-------------------------------------------------------------------------

register_decl :: RT.Rec -> [String]
register_decl r =
    case (RT.tpe r) of 
      (TT.Format tname sz _ tdesc _ _) -> 
          [ C.multi_comment (printf "Register %s (%s); type %s (%s)" 
                                        (RT.name r)
                                        (RT.desc r)
                                        (RT.typename r)
                                        (TT.tt_desc (RT.tpe r))),
            -- (show (RT.extents r)),
            (register_dump r),
            (register_length r),
            (register_read_raw r),
            (register_read r),
            (register_write_raw r),
            (register_write r),
            (register_write_fields r),
            (register_print_fn r)
          ] 
      (TT.Primitive tname sz _) ->
          [ (C.multi_comment (printf "Register %s (%s); type %s" 
                                     (RT.name r)
                                     (RT.desc r)
                                     (RT.typename r))),
            -- (show (RT.extents r)),
            (register_length r),
            (register_read_raw r),
            (register_read_builtin r),
            (register_write_raw r),
            (register_write_builtin r),
            (register_write_fields r),
            (register_print_fn r)
          ] 

-- Refer to a shadow copy of the register
reg_shadow_ref r = "_dev->" ++
                   (shadow (RT.name r)) ++
                   (if RT.is_array r then "[_i]" else "")
          
-- Get the arguments right for array- and non-array registers
register_args pre r post = 
    pre ++ 
            [ (dev_ptr, "_dev") ] ++
            ( if RT.is_array r then [("int","_i")] else [] ) ++ post

-- Now, code to read and write raw values.  These are inlined into the
-- various "real" routines below
loc_read :: RT.Rec -> String
loc_read r = 
    case RT.spc r of
      (Builtin n _ t) -> 
          (printf "mackerel_read_%s_%s(_dev->%s,%s)" 
                  n (show (RT.size r)) (RT.base r)
                        (loc_array_off t (RT.offset r) (RT.arr r) (RT.size r)))
      (Defined n a _ t p) -> 
          (printf "__DP(%s_read_%s)(_dev, %s)" 
                      n (show (RT.size r)) 
                      (loc_array_off t (RT.offset r) (RT.arr r) (RT.size r)))

loc_array_off :: Space.SpaceType -> Integer -> ArrayLoc -> Integer -> String
loc_array_off _ off (ArrayListLoc []) _ = 
    printf "(0x%0x)" off
loc_array_off (Space.BYTEWISE s) off (ArrayStepLoc num 0) sz = 
    printf "(0x%0x) + (_i *(%s/8))" off (show (sz `div` s))
loc_array_off Space.VALUEWISE off (ArrayStepLoc num 0) _ = 
    printf "(0x%0x) + (_i)" off
loc_array_off _ off (ArrayStepLoc num step) _ = 
    printf "(0x%0x) + (_i * %d)" off step
loc_array_off t off (ArrayListLoc locations) sz = 
    (show locations)


loc_write :: RT.Rec -> String -> String
loc_write r val = 
    case RT.spc r of
      (Builtin n _ t) -> 
          (printf "mackerel_write_%s_%s(_dev->%s,%s,%s)" 
                  n 
                  (show (RT.size r)) 
                  (RT.base r)
                  (loc_array_off t (RT.offset r) (RT.arr r) (RT.size r))
                  val
          )
      (Defined n a _ t p) -> 
          (printf "__DP(%s_write_%s)(_dev, %s,%s)" 
                  n 
                  (show (RT.size r)) 
                  (loc_array_off t (RT.offset r) (RT.arr r) (RT.size r))
                  val
          )

register_length r 
    | RT.is_array r = C.constint (reg_len (RT.name r)) (RT.num_elements r)
    | otherwise = ""

register_dump r =
    C.multi_comment ( "Dump of fields for register: " ++ (RT.name r) ++ "\n" ++
                      unlines([ field_dump f | f <- (RT.fl r)] ))

field_dump :: Fields.Rec -> String
field_dump f =
    (printf "  %s (size %d, offset %d):\t %s\t  %s") 
    (Fields.name f)
    (Fields.size f)
    (Fields.offset f)
    (show $ Fields.attr f)
    (Fields.desc f )

register_read_raw r
    | RT.is_readable r = 
        C.inline (round_field_size (RT.size r)) (reg_rd_raw (RT.name r) )
             (register_args [] r [])
             [ "return " ++ (loc_read r) ++ ";" ]
    | otherwise = 
        C.comment( "Register " ++ (RT.name r) ++ " is not readable" )

register_read r
    | RT.is_readable r =
        C.inline (reg_t (RT.typename r)) (reg_rd (RT.name r)) 
             (register_args [] r [])
             ( [ (reg_un (RT.typename r)) ++ "  u;",
                 "u.raw = " ++ (loc_read r) ++ ";",
                 "return u.val;" ] )
    | otherwise = 
        C.inline (reg_t (RT.typename r)) (reg_rds (RT.name r)) 
             (register_args [] r [])
             ( [ "return " ++ (reg_shadow_ref r) ++ ".val;" ] )

register_read_builtin r
    | RT.is_readable r =
        C.inline (builtin_to_c (RT.typename r)) (reg_rd (RT.name r)) 
             (register_args [] r [])
             ( [ "return " ++ (loc_read r) ++ ";" ] )
    | otherwise = 
        C.inline (builtin_to_c (RT.typename r)) (reg_rds (RT.name r))
             (register_args [] r [])
             ( [ "return " ++ (reg_shadow_ref r) ++ ";" ] )

register_write_raw r
    | RT.is_writeable r = 
        C.inline "void" (reg_wr_raw (RT.name r)) 
             (register_args [] r [ ((round_field_size (RT.size r)), "val") ])
             [ (loc_write r "val") ++ ";" ]
    | otherwise = 
        C.comment( "Register " ++ (RT.name r) ++ " is not writeable" )

register_write r
    | RT.is_writeable r = 
        C.inline "void" (reg_wr (RT.name r)) 
             (register_args [] r [ ((reg_t (RT.typename r)), "val") ])
             ( [ (reg_un (RT.typename r)) ++ "  u;" ] 
               ++ 
               (reg_write_init_value r)
               ++
               (reg_write_mbz_value r)
               ++
               (reg_write_mb1_value r)
               ++
               [ (loc_write r "u.raw") ++ ";" ]
               ++
               ( if RT.needs_shadow r 
                 then [ (reg_shadow_ref r) ++ ".val = u.val;" ]
                 else [] )
             )
    | otherwise = 
        []

register_write_fields r = 
    unlines [ field_write_fn r f | f <- (RT.fl r), Fields.is_writeable f ]

-- This is actually WRONG.  We should actually initialize the value to
-- be written properly.  
-- 1) If there are write-only fields (other than f) load u.raw from shadow. 
-- 2) If there are rw fields (other than f) load u.raw from register.
-- 2a) If there are both, copy fields from one to the other. 
-- 3) Initialize the other fields (mbz, mb1, f)
-- 4) Do the write. 
field_write_fn r f = 
    C.inline "void" (field_wr (RT.name r) (Fields.name f))
         (register_args [] r [ (field_type f, "val") ])
             ( [ (reg_un (RT.typename r)) ++ "  u;" ] 
               ++ 
               (if (any (\x -> ((Fields.name x) /= (Fields.name f) && field_must_be_preread x)) (RT.fl r) )
               then [ "u.raw = " ++ (loc_read r) ++ ";" ]
               else [])
               ++
               [ copy_shadow_field x r | x <- RT.fl r, 
                                            (Fields.name x) /= (Fields.name f),
                                            Fields.is_writeonly x 
               ]
               ++
               (reg_write_mbz_value r)
               ++
               (reg_write_mb1_value r)
               ++
               [ "u.val." ++ (Fields.name f) ++ " = val;" ]
               ++

               [ (loc_write r "u.raw") ++ ";" ]
               ++
               ( if RT.needs_shadow r 
                 then [ (reg_shadow_ref r) ++ ".val = u.val;" ]
                 else [] )
             )
copy_shadow_field :: Fields.Rec -> RT.Rec -> String
copy_shadow_field f r = printf "u.val.%s = %s.val.%s;" 
                        (Fields.name f) 
                        (reg_shadow_ref r) 
                        (Fields.name f)

field_must_be_preread :: Fields.Rec -> Bool
field_must_be_preread f 
    | Fields.attr f == RSVD = True
    | otherwise = (Fields.is_readable f) && (Fields.is_writeable f)

register_write_builtin r
    | RT.is_writeable r = 
        C.inline "void" (reg_wr (RT.name r))
             (register_args [] r [ ((builtin_to_c (RT.typename r)), "val") ])
              ( [ (loc_write r "val") ++ ";" ] 
                ++
                (if RT.needs_shadow r 
                 then [ (reg_shadow_ref r) ++ " = val;" ]
                 else []
                )
              )
    | otherwise = 
        []

reg_write_init_value r
    | RT.needs_read_before_write r = 
        (if RT.is_readable r
         then "u.raw = " ++ (loc_read r) ++ ";"
         else "u.raw = " ++ (reg_shadow_ref r) ++ ".raw;" )
      :[ printf "u.val.%s \t= val.%s;" (Fields.name f) (Fields.name f)
             | f <- (RT.fl r), not (Fields.is_rsvd f) ]
    | otherwise =
        [ "u.val = val;" ]

reg_write_mbz_value r = 
    [ (printf "u.val.%s \t= 0;") (Fields.name f) 
          | f <- RT.fl r, (Fields.attr f) == MBZ ]

reg_write_mb1_value r = 
    [ (printf "u.val.%s \t= -1;") (Fields.name f) 
          | f <- RT.fl r, (Fields.attr f) == MB1 ]

-- Print out a value of the register type
register_print_fn :: RT.Rec -> String
register_print_fn r 
    | RT.is_array r = 
        (register_print_array_element r) ++ (register_print_array r)
    | otherwise = 
        (register_print_single r) 

register_print_array_element r = 
    C.inline "int" (reg_pri (RT.name r))
         ( register_args [ ("char *","s"), ("size_t","sz") ] r [] )
         ( ["int r=0;",
            "int _avail, _rc;" ] ++
           (register_print_init r) ++
           (C.snprintf (printf "\"Register %s[%%d] (%s):\", _i" (RT.name r) (percent_escape (RT.desc r)))) ++ 
           (register_print_value r) ++
           ["return r;" ]
         )

register_print_array r =
    C.inline "int" (reg_pr (RT.name r))
         [ ("char *","s"), ("size_t","sz"), (dev_ptr, "_dev") ]
         ( ["int r=0;",
            "int _avail, _rc;" ] ++
           (C.forloop "int _i=0" 
                 (printf "_i < 0x%0x" (RT.num_elements r))
                 "_i++" 
                 (C.snlike (reg_pri (RT.name r)) "_dev, _i" ) ) ++
           ["return r;"]
         )

register_print_single r = 
    C.inline "int" (reg_pr (RT.name r))
         ( register_args [ ("char *","s"), ("size_t","sz") ] r [] )
         ( ["int r=0;",
            "int _avail, _rc;" ] ++
           (register_print_init r) ++
           (C.snputsq (printf "Register %s (%s):" (RT.name r) (percent_escape (RT.desc r)))) ++ 
           (register_print_value r) ++
           ["return r;" ]
         )

register_print_value r
    | TT.is_primitive (RT.tpe r) = 
        register_print_primitive r
    | otherwise =
        ( C.snputsq "\\n" )
        ++ concat [ regfield_print r f | f <- (RT.fl r) ]

register_print_primitive r 
    | RT.needs_shadow r = 
        C.snprintf (printf "\"\\t0x%s (SHADOW copy)\\n\", %s" 
                               (field_fmt_str (RT.size r))
                               (reg_shadow_ref r) )
    | otherwise = 
        C.snprintf (printf "\"\\t0x%s\\n\", %s" 
                               (field_fmt_str (RT.size r))
                               (loc_read r))

register_print_init r 
    | not (RT.is_readable r) = 
        [ C.comment "register is not readable" ]
    | TT.is_primitive (RT.tpe r) =
        [ C.comment "register is primitive type" ]
    | otherwise = 
        [ (reg_un (RT.typename r)) ++ "  u;",
          "u.raw = " ++ (loc_read r) ++ ";" ]

regfield_print :: RT.Rec -> Fields.Rec -> [ String ]
regfield_print reg f
    | Fields.is_anon f = 
        [ C.comment "skipping anonymous field" ]
    | otherwise = 
        C.block ( [ (printf "%s pv = (%s)%s.val.%s;" 
                                (round_field_size (Fields.size f))
                                (round_field_size (Fields.size f))
                                (if Fields.is_writeonly f
                                 then
                                     (reg_shadow_ref reg)
                                 else
                                     "u"
                                )
                                (Fields.name f))
                  ] ++
                  ( C.snprintf (printf "\" %s =\\t0x%s (%s%s\", pv" 
                                           (Fields.name f)
                                           (field_fmt_str (Fields.size f))
                                           (if Fields.is_writeonly f
                                            then "SHADOW of "
                                            else "")
                                           (percent_escape (Fields.desc f)))
                  ) 
                  ++ 
                  (case (Fields.tpe f) of 
                     Nothing -> (C.snputsq ")\\n" )
                     Just t -> ( (C.snputsq ": ") 
                                 ++
                                 (C.snlike  (enum_pr t) "pv") 
                                 ++
                                 (C.snputsq ")\\n"))
                  )
                )

