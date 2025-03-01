# 
# KDOM IDL parser
#
# Copyright (C) 2005 Nikolas Zimmermann <wildfox@kde.org>
# 
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Library General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
# 
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Library General Public License for more details.
# 
# You should have received a copy of the GNU Library General Public License
# along with this library; see the file COPYING.LIB.  If not, write to
# the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
# Boston, MA 02110-1301, USA.
# 

package IDLParser;

use strict;

use Carp qw<longmess>;
use Data::Dumper;

use preprocessor;
use Class::Struct;

use constant StringToken => 0;
use constant IntegerToken => 1;
use constant FloatToken => 2;
use constant IdentifierToken => 3;
use constant OtherToken => 4;
use constant EmptyToken => 5;

# Used to represent a parsed IDL document
struct( IDLDocument => {
    interfaces => '@', # List of 'IDLInterface'
    enumerations => '@', # List of 'IDLEnum'
    dictionaries => '@', # List of 'IDLDictionary'
    callbackFunctions => '@', # List of 'IDLCallbackFunction'
    fileName => '$',
});

# https://heycam.github.io/webidl/#idl-types
struct( IDLType => {
    name =>         '$', # Type identifier
    isNullable =>   '$', # Is the type Nullable (T?)
    isUnion =>      '$', # Is the type a union (T or U)
    subtypes =>     '@', # Array of subtypes, only valid if isUnion or sequence
    extendedAttributes => '%',
});

# Used to represent 'interface' blocks
struct( IDLInterface => {
    type => 'IDLType',
    parentType => 'IDLType',
    constants => '@',    # List of 'IDLConstant'
    functions => '@',    # List of 'IDLOperation'
    anonymousFunctions => '@', # List of 'IDLOperation'
    attributes => '@',    # List of 'IDLAttribute'
    constructors => '@', # Constructors, list of 'IDLOperation'
    customConstructors => '@', # Custom constructors, list of 'IDLOperation'
    isException => '$', # Used for exception interfaces
    isCallback => '$', # Used for callback interfaces
    isPartial => '$', # Used for partial interfaces
    iterable => '$', # Used for iterable interfaces
    mapLike => '$', # Used for mapLike interfaces
    serializable => '$', # Used for serializable interfaces
    extendedAttributes => '$',
});

# Used to represent an argument to a IDLOperation.
struct( IDLArgument => {
    name => '$',
    type => 'IDLType',
    isVariadic => '$',
    isOptional => '$',
    default => '$',
    extendedAttributes => '$',
});

# https://heycam.github.io/webidl/#idl-operations
struct( IDLOperation => {
    name => '$',
    type => 'IDLType', # Return type
    arguments => '@', # List of 'IDLArgument'
    isStatic => '$',
    isIterable => '$',
    isSerializer => '$',
    isMapLike => '$',
    specials => '@',
    extendedAttributes => '$',
});


# https://heycam.github.io/webidl/#idl-attributes
struct( IDLAttribute => {
    name => '$',
    type => 'IDLType',
    isStatic => '$',
    isMapLike => '$',
    isStringifier => '$',
    isReadOnly => '$',
    extendedAttributes => '$',
});

# https://heycam.github.io/webidl/#idl-iterable
struct( IDLIterable => {
    isKeyValue => '$',
    keyType => 'IDLType',
    valueType => 'IDLType',
    functions => '@', # Iterable functions (entries, keys, values, [Symbol.Iterator], forEach)
    extendedAttributes => '$',
});

# https://heycam.github.io/webidl/#es-maplike
struct( IDLMapLike => {
    isReadOnly => '$',
    keyType => 'IDLType',
    valueType => 'IDLType',
    attributes => '@', # MapLike attributes (size)
    functions => '@', # MapLike functions (entries, keys, values, forEach, get, has and if not readonly, delete, set and clear)
    extendedAttributes => '$',
});

# https://heycam.github.io/webidl/#idl-serializers
struct( IDLSerializable => {
    attributes => '@', # List of attributes to serialize
    hasAttribute => '$', # serializer = { attribute }
    hasInherit => '$', # serializer = { inherit }
    hasGetter => '$', # serializer = { getter }
    functions => '@', # toJSON function
});

# https://heycam.github.io/webidl/#idl-constants
struct( IDLConstant => {
    name => '$',
    type => 'IDLType',
    value => '$',
    extendedAttributes => '$',
});

# https://heycam.github.io/webidl/#idl-enums
struct( IDLEnum => {
    name => '$',
    type => 'IDLType',
    values => '@',
    extendedAttributes => '$',
});

# https://heycam.github.io/webidl/#dfn-dictionary-member
struct( IDLDictionaryMember => {
    name => '$',
    type => 'IDLType',
    isRequired => '$',
    default => '$',
    extendedAttributes => '$',
});

# https://heycam.github.io/webidl/#idl-dictionaries
struct( IDLDictionary => {
    type => 'IDLType',
    parentType => 'IDLType',
    members => '@', # List of 'IDLDictionaryMember'
    extendedAttributes => '$',
});

# https://heycam.github.io/webidl/#idl-callback-functions
struct( IDLCallbackFunction => {
    type => '$',
    operation => 'IDLOperation',
    extendedAttributes => '$',
});

# https://heycam.github.io/webidl/#idl-typedefs
struct( IDLTypedef => {
    type => 'IDLType',
});

struct( Token => {
    type => '$', # type of token
    value => '$' # value of token
});

# Maps 'typedef name' -> Typedef
my %typedefs = ();

sub new {
    my $class = shift;

    my $emptyToken = Token->new();
    $emptyToken->type(EmptyToken);
    $emptyToken->value("empty");

    my $self = {
        DocumentContent => "",
        EmptyToken => $emptyToken,
        NextToken => $emptyToken,
        Token => $emptyToken,
        Line => "",
        LineNumber => 1,
        ExtendedAttributeMap => ""
    };
    return bless $self, $class;
}

sub assert
{
    my $message = shift;

    my $mess = longmess();
    print Dumper($mess);

    die $message;
}

sub assertTokenValue
{
    my $self = shift;
    my $token = shift;
    my $value = shift;
    my $line = shift;
    my $msg = "Next token should be " . $value . ", but " . $token->value() . " on line " . $self->{Line};
    if (defined ($line)) {
        $msg .= " IDLParser.pm:" . $line;
    }

    assert $msg unless $token->value() eq $value;
}

sub assertTokenType
{
    my $self = shift;
    my $token = shift;
    my $type = shift;
    
    assert "Next token's type should be " . $type . ", but " . $token->type() . " on line " . $self->{Line} unless $token->type() eq $type;
}

sub assertUnexpectedToken
{
    my $self = shift;
    my $token = shift;
    my $line = shift;
    my $msg = "Unexpected token " . $token . " on line " . $self->{Line};
    if (defined ($line)) {
        $msg .= " IDLParser.pm:" . $line;
    }

    assert $msg;
}

sub assertExtendedAttributesValidForContext
{
    my $self = shift;
    my $extendedAttributeList = shift;
    my @contexts = @_;

    for my $extendedAttribute (keys %{$extendedAttributeList}) {
        # FIXME: Should this be done here, or when parsing the exteded attribute itself?
        # Either way, we should add validation of the values, if any, at the same place.

        # Extended attribute parsing collapses multiple 'Constructor' or 'CustomConstructor'
        # attributes into a single plural version. Eventually, it would be nice if that conversion
        # hapened later, and the parser kept things relatively simply, but for now, we just undo
        # this transformation for the type check.
        if ($extendedAttribute eq "Constructors") {
            $extendedAttribute = "Constructor";
        } elsif ($extendedAttribute eq "CustomConstructors") {
            $extendedAttribute = "CustomConstructor";
        }

        if (!exists $self->{ExtendedAttributeMap}->{$extendedAttribute}) {
            assert "Unknown extended attribute: '${extendedAttribute}'";
        }

        my $foundAllowedContext = 0;
        for my $contextAllowed (@{$self->{ExtendedAttributeMap}->{$extendedAttribute}->{"contextsAllowed"}}) {
            for my $context (@contexts) {
                if ($contextAllowed eq $context) {
                    $foundAllowedContext = 1;
                    last;
                }
            }
        }

        if (!$foundAllowedContext) {
            if (scalar(@contexts) == 1) {
                assert "Extended attribute '${extendedAttribute}' used in invalid context '" . $contexts[0] . "'";
            } else {
                # FIXME: Improved this error message a bit.
                assert "Extended attribute '${extendedAttribute}' used in invalid context";
            }
        }
    }
}

sub Parse
{
    my $self = shift;
    my $fileName = shift;
    my $defines = shift;
    my $preprocessor = shift;
    my $idlAttributes = shift;

    my @definitions = ();

    my @lines = applyPreprocessor($fileName, $defines, $preprocessor);
    $self->{Line} = $lines[0];
    $self->{DocumentContent} = join(' ', @lines);
    $self->{ExtendedAttributeMap} = $idlAttributes;

    $self->getToken();
    eval {
        my $result = $self->parseDefinitions();
        push(@definitions, @{$result});

        my $next = $self->nextToken();
        $self->assertTokenType($next, EmptyToken);
    };
    assert $@ . " in $fileName" if $@;

    my $document = IDLDocument->new();
    $document->fileName($fileName);
    foreach my $definition (@definitions) {
        if (ref($definition) eq "IDLInterface") {
            push(@{$document->interfaces}, $definition);
        } elsif (ref($definition) eq "IDLEnum") {
            push(@{$document->enumerations}, $definition);
        } elsif (ref($definition) eq "IDLDictionary") {
            push(@{$document->dictionaries}, $definition);
        } elsif (ref($definition) eq "IDLCallbackFunction") {
            push(@{$document->callbackFunctions}, $definition);
        } else {
            die "Unrecognized IDL definition kind: \"" . ref($definition) . "\"";
        }
    }
    return $document;
}

sub nextToken
{
    my $self = shift;
    return $self->{NextToken};
}

sub getToken
{
    my $self = shift;
    $self->{Token} = $self->{NextToken};
    $self->{NextToken} = $self->getTokenInternal();
    return $self->{Token};
}

my $whitespaceTokenPattern = '^[\t\n\r ]*[\n\r]';
my $floatTokenPattern = '^(-?(([0-9]+\.[0-9]*|[0-9]*\.[0-9]+)([Ee][+-]?[0-9]+)?|[0-9]+[Ee][+-]?[0-9]+))';
my $integerTokenPattern = '^(-?[1-9][0-9]*|-?0[Xx][0-9A-Fa-f]+|-?0[0-7]*)';
my $stringTokenPattern = '^(\"[^\"]*\")';
my $identifierTokenPattern = '^([A-Z_a-z][0-9A-Z_a-z]*)';
my $otherTokenPattern = '^(\.\.\.|[^\t\n\r 0-9A-Z_a-z])';

sub getTokenInternal
{
    my $self = shift;

    if ($self->{DocumentContent} =~ /$whitespaceTokenPattern/) {
        $self->{DocumentContent} =~ s/($whitespaceTokenPattern)//;
        my $skipped = $1;
        $self->{LineNumber}++ while ($skipped =~ /\n/g);
        if ($self->{DocumentContent} =~ /^([^\n\r]+)/) {
            $self->{Line} = $self->{LineNumber} . ":" . $1;
        } else {
            $self->{Line} = "Unknown";
        }
    }
    $self->{DocumentContent} =~ s/^([\t\n\r ]+)//;
    if ($self->{DocumentContent} eq "") {
        return $self->{EmptyToken};
    }

    my $token = Token->new();
    if ($self->{DocumentContent} =~ /$floatTokenPattern/) {
        $token->type(FloatToken);
        $token->value($1);
        $self->{DocumentContent} =~ s/$floatTokenPattern//;
        return $token;
    }
    if ($self->{DocumentContent} =~ /$integerTokenPattern/) {
        $token->type(IntegerToken);
        $token->value($1);
        $self->{DocumentContent} =~ s/$integerTokenPattern//;
        return $token;
    }
    if ($self->{DocumentContent} =~ /$stringTokenPattern/) {
        $token->type(StringToken);
        $token->value($1);
        $self->{DocumentContent} =~ s/$stringTokenPattern//;
        return $token;
    }
    if ($self->{DocumentContent} =~ /$identifierTokenPattern/) {
        $token->type(IdentifierToken);
        $token->value($1);
        $self->{DocumentContent} =~ s/$identifierTokenPattern//;
        return $token;
    }
    if ($self->{DocumentContent} =~ /$otherTokenPattern/) {
        $token->type(OtherToken);
        $token->value($1);
        $self->{DocumentContent} =~ s/$otherTokenPattern//;
        return $token;
    }
    die "Failed in tokenizing at " . $self->{Line};
}

sub unquoteString
{
    my $self = shift;
    my $quotedString = shift;
    if ($quotedString =~ /^"([^"]*)"$/) {
        return $1;
    }
    die "Failed to parse string (" . $quotedString . ") at " . $self->{Line};
}

sub identifierRemoveNullablePrefix
{
    my $type = shift;
    $type =~ s/^_//;
    return $type;
}

sub copyExtendedAttributes
{
    my $extendedAttributeList = shift;
    my $attr = shift;

    for my $key (keys %{$attr}) {
        if ($key eq "Constructor") {
            push(@{$extendedAttributeList->{"Constructors"}}, $attr->{$key});
        } elsif ($key eq "Constructors") {
            my @constructors = @{$attr->{$key}};
            foreach my $constructor (@constructors) {
                push(@{$extendedAttributeList->{"Constructors"}}, $constructor);
            }
        } elsif ($key eq "CustomConstructor") {
            push(@{$extendedAttributeList->{"CustomConstructors"}}, $attr->{$key});
        } elsif ($key eq "CustomConstructors") {
           my @customConstructors = @{$attr->{$key}};
            foreach my $customConstructor (@customConstructors) {
                push(@{$extendedAttributeList->{"CustomConstructors"}}, $customConstructor);
            }
        } else {
            $extendedAttributeList->{$key} = $attr->{$key};
        }
    }
}

sub isExtendedAttributeApplicableToTypes
{
    my $self = shift;
    my $extendedAttribute = shift;

    if (!exists $self->{ExtendedAttributeMap}->{$extendedAttribute}) {
        assert "Unknown extended attribute: '${extendedAttribute}'";
    }

    for my $contextAllowed (@{$self->{ExtendedAttributeMap}->{$extendedAttribute}->{"contextsAllowed"}}) {
        if ($contextAllowed eq "type") {
            return 1;
        }
    }

    return 0;
}

sub moveExtendedAttributesApplicableToTypes
{
    my $self = shift;
    my $type = shift;
    my $extendedAttributeList = shift;

    for my $key (keys %{$extendedAttributeList}) {
        if ($self->isExtendedAttributeApplicableToTypes($key)) {
            if (!defined $type->extendedAttributes->{$key}) {
                $type->extendedAttributes->{$key} = $extendedAttributeList->{$key};
            }
            delete $extendedAttributeList->{$key};
        }
    }
}

sub typeDescription
{
    my $type = shift;

    if (scalar @{$type->subtypes}) {
        return $type->name . '<' . join(', ', map { typeDescription($_) } @{$type->subtypes}) . '>' . ($type->isNullable ? "?" : "");
    }

    return $type->name . ($type->isNullable ? "?" : "");
}

sub makeSimpleType
{
    my $typeName = shift;

    return IDLType->new(name => $typeName);
}

sub cloneType
{
    my $self = shift;
    my $type = shift;

    my $clonedType = IDLType->new();
    $clonedType->name($type->name);
    $clonedType->isNullable($type->isNullable);
    $clonedType->isUnion($type->isUnion);

    copyExtendedAttributes($clonedType->extendedAttributes, $type->extendedAttributes);

    foreach my $subtype (@{$type->subtypes}) {
        push(@{$clonedType->subtypes}, $self->cloneType($subtype));
    }

    return $clonedType;
}

my $nextAttribute_1 = '^(attribute|inherit)$';
my $nextPrimitiveType_1 = '^(int|long|short|unsigned)$';
my $nextPrimitiveType_2 = '^(double|float|unrestricted)$';
my $nextArgumentList_1 = '^(\(|ByteString|DOMString|USVString|Date|\[|any|boolean|byte|double|float|in|long|object|octet|optional|sequence|short|unrestricted|unsigned)$';
my $nextNonAnyType_1 = '^(boolean|byte|double|float|long|octet|short|unrestricted|unsigned)$';
my $nextStringType_1 = '^(ByteString|DOMString|USVString)$';
my $nextInterfaceMember_1 = '^(\(|ByteString|DOMString|USVString|Date|any|attribute|boolean|byte|creator|deleter|double|float|getter|inherit|legacycaller|long|object|octet|readonly|sequence|serializer|setter|short|static|stringifier|unrestricted|unsigned|void)$';
my $nextAttributeOrOperation_1 = '^(static|stringifier)$';
my $nextAttributeOrOperation_2 = '^(\(|ByteString|DOMString|USVString|Date|any|boolean|byte|creator|deleter|double|float|getter|legacycaller|long|object|octet|sequence|setter|short|unrestricted|unsigned|void)$';
my $nextUnrestrictedFloatType_1 = '^(double|float)$';
my $nextExtendedAttributeRest3_1 = '^(\,|\])$';
my $nextExceptionField_1 = '^(\(|ByteString|DOMString|USVString|Date|any|boolean|byte|double|float|long|object|octet|sequence|short|unrestricted|unsigned)$';
my $nextType_1 = '^(ByteString|DOMString|USVString|Date|any|boolean|byte|double|float|long|object|octet|sequence|short|unrestricted|unsigned)$';
my $nextSpecials_1 = '^(creator|deleter|getter|legacycaller|setter)$';
my $nextDefinitions_1 = '^(callback|dictionary|enum|exception|interface|partial|typedef)$';
my $nextExceptionMembers_1 = '^(\(|ByteString|DOMString|USVString|Date|\[|any|boolean|byte|const|double|float|long|object|octet|optional|sequence|short|unrestricted|unsigned)$';
my $nextInterfaceMembers_1 = '^(\(|ByteString|DOMString|USVString|Date|any|attribute|boolean|byte|const|creator|deleter|double|float|getter|inherit|legacycaller|long|object|octet|readonly|sequence|serializer|setter|short|static|stringifier|unrestricted|unsigned|void)$';
my $nextSingleType_1 = '^(ByteString|DOMString|USVString|Date|boolean|byte|double|float|long|object|octet|sequence|short|unrestricted|unsigned)$';
my $nextArgumentName_1 = '^(attribute|callback|const|creator|deleter|dictionary|enum|exception|getter|implements|inherit|interface|legacycaller|partial|serializer|setter|static|stringifier|typedef|unrestricted)$';
my $nextConstValue_1 = '^(false|true)$';
my $nextConstValue_2 = '^(-|Infinity|NaN)$';
my $nextDefinition_1 = '^(callback|interface)$';
my $nextAttributeOrOperationRest_1 = '^(\(|ByteString|DOMString|USVString|Date|any|boolean|byte|double|float|long|object|octet|sequence|short|unrestricted|unsigned|void)$';
my $nextUnsignedIntegerType_1 = '^(long|short)$';
my $nextDefaultValue_1 = '^(-|Infinity|NaN|false|null|true)$';


sub parseDefinitions
{
    my $self = shift;
    my @definitions = ();

    while (1) {
        my $extendedAttributeList = $self->parseExtendedAttributeListAllowEmpty();
        my $next = $self->nextToken();
        my $definition;
        if ($next->type() == IdentifierToken || $next->value() =~ /$nextDefinitions_1/) {
            $definition = $self->parseDefinition($extendedAttributeList);
        } else {
            last;
        }
        if (defined ($definition)) {
            push(@definitions, $definition);
        }
    }
    $self->applyTypedefs(\@definitions);
    return \@definitions;
}

sub applyTypedefs
{
    my $self = shift;
    my $definitions = shift;
   
    if (!%typedefs) {
        return;
    }
    
    # FIXME: Add support for applying typedefs to IDLIterable.
    foreach my $definition (@$definitions) {
        if (ref($definition) eq "IDLInterface") {
            foreach my $constant (@{$definition->constants}) {
                $constant->type($self->typeByApplyingTypedefs($constant->type));
            }
            foreach my $attribute (@{$definition->attributes}) {
                $attribute->type($self->typeByApplyingTypedefs($attribute->type));
            }
            foreach my $operation (@{$definition->functions}, @{$definition->anonymousFunctions}, @{$definition->constructors}, @{$definition->customConstructors}) {
                $self->applyTypedefsToOperation($operation);
            }
        } elsif (ref($definition) eq "IDLDictionary") {
            foreach my $member (@{$definition->members}) {
                $member->type($self->typeByApplyingTypedefs($member->type));
            }
        } elsif (ref($definition) eq "IDLCallbackFunction") {
            $self->applyTypedefsToOperation($definition->operation);
        }
    }
}

sub applyTypedefsToOperation
{
    my $self = shift;
    my $operation = shift;

    if ($operation->type) {
        $operation->type($self->typeByApplyingTypedefs($operation->type));
    }

    foreach my $argument (@{$operation->arguments}) {
        $argument->type($self->typeByApplyingTypedefs($argument->type));
    }
}

sub typeByApplyingTypedefs
{
    my $self = shift;
    my $type = shift;

    assert("Missing type") if !$type;

    my $numberOfSubtypes = scalar @{$type->subtypes};
    if ($numberOfSubtypes) {
        for my $i (0..$numberOfSubtypes - 1) {
            my $subtype = @{$type->subtypes}[$i];
            my $replacementSubtype = $self->typeByApplyingTypedefs($subtype);
            @{$type->subtypes}[$i] = $replacementSubtype
        }

        return $type;
    }

    if (exists $typedefs{$type->name}) {
        my $typedef = $typedefs{$type->name};

        my $clonedType = $self->cloneType($typedef->type);
        $clonedType->isNullable($clonedType->isNullable || $type->isNullable);
        $self->moveExtendedAttributesApplicableToTypes($clonedType, $type->extendedAttributes);

        return $self->typeByApplyingTypedefs($clonedType);
    }
    
    return $type;
}

sub parseDefinition
{
    my $self = shift;
    my $extendedAttributeList = shift;

    my $next = $self->nextToken();
    if ($next->value() =~ /$nextDefinition_1/) {
        return $self->parseCallbackOrInterface($extendedAttributeList);
    }
    if ($next->value() eq "partial") {
        return $self->parsePartial($extendedAttributeList);
    }
    if ($next->value() eq "dictionary") {
        return $self->parseDictionary($extendedAttributeList);
    }
    if ($next->value() eq "exception") {
        return $self->parseException($extendedAttributeList);
    }
    if ($next->value() eq "enum") {
        return $self->parseEnum($extendedAttributeList);
    }
    if ($next->value() eq "typedef") {
        return $self->parseTypedef($extendedAttributeList);
    }
    if ($next->type() == IdentifierToken) {
        return $self->parseImplementsStatement($extendedAttributeList);
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseCallbackOrInterface
{
    my $self = shift;
    my $extendedAttributeList = shift;

    my $next = $self->nextToken();
    if ($next->value() eq "callback") {
        $self->assertTokenValue($self->getToken(), "callback", __LINE__);
        return $self->parseCallbackRestOrInterface($extendedAttributeList);
    }
    if ($next->value() eq "interface") {
        return $self->parseInterface($extendedAttributeList);
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseCallbackRestOrInterface
{
    my $self = shift;
    my $extendedAttributeList = shift;

    my $next = $self->nextToken();
    if ($next->value() eq "interface") {
        my $interface = $self->parseInterface($extendedAttributeList);
        $interface->isCallback(1);
        return $interface;
    }
    if ($next->type() == IdentifierToken) {
        return $self->parseCallbackRest($extendedAttributeList);
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseInterface
{
    my $self = shift;
    my $extendedAttributeList = shift;

    my $next = $self->nextToken();
    if ($next->value() eq "interface") {
        my $interface = IDLInterface->new();
        $self->assertTokenValue($self->getToken(), "interface", __LINE__);
        my $interfaceNameToken = $self->getToken();
        $self->assertTokenType($interfaceNameToken, IdentifierToken);
        
        my $name = identifierRemoveNullablePrefix($interfaceNameToken->value());
        $interface->type(makeSimpleType($name));

        $next = $self->nextToken();
        if ($next->value() eq ":") {
            my $parent = $self->parseInheritance();
            $interface->parentType(makeSimpleType($parent));
        }

        $self->assertTokenValue($self->getToken(), "{", __LINE__);
        my $interfaceMembers = $self->parseInterfaceMembers();
        $self->assertTokenValue($self->getToken(), "}", __LINE__);
        $self->assertTokenValue($self->getToken(), ";", __LINE__);
        applyMemberList($interface, $interfaceMembers);

        $self->assertExtendedAttributesValidForContext($extendedAttributeList, "interface");
        applyExtendedAttributeList($interface, $extendedAttributeList);

        return $interface;
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parsePartial
{
    my $self = shift;
    my $extendedAttributeList = shift;

    my $next = $self->nextToken();
    if ($next->value() eq "partial") {
        $self->assertTokenValue($self->getToken(), "partial", __LINE__);
        return $self->parsePartialDefinition($extendedAttributeList);
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parsePartialDefinition
{
    my $self = shift;
    my $extendedAttributeList = shift;

    my $next = $self->nextToken();
    if ($next->value() eq "interface") {
        my $interface = $self->parseInterface($extendedAttributeList);
        $interface->isPartial(1);
        return $interface;
    }
    if ($next->value() eq "dictionary") {
        return $self->parsePartialDictionary($extendedAttributeList);
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parsePartialInterface
{
    my $self = shift;
    my $extendedAttributeList = shift;

    my $next = $self->nextToken();
    if ($next->value() eq "interface") {
        $self->assertTokenValue($self->getToken(), "interface", __LINE__);
        $self->assertTokenType($self->getToken(), IdentifierToken);
        $self->assertTokenValue($self->getToken(), "{", __LINE__);
        $self->parseInterfaceMembers();
        $self->assertTokenValue($self->getToken(), "}", __LINE__);
        $self->assertTokenValue($self->getToken(), ";", __LINE__);
        return;
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseInterfaceMembers
{
    my $self = shift;
    my @interfaceMembers = ();

    while (1) {
        my $extendedAttributeList = $self->parseExtendedAttributeListAllowEmpty();
        my $next = $self->nextToken();
        my $interfaceMember;

        if ($next->type() == IdentifierToken || $next->value() =~ /$nextInterfaceMembers_1/) {
            $interfaceMember = $self->parseInterfaceMember($extendedAttributeList);
        } else {
            last;
        }
        if (defined $interfaceMember) {
            push(@interfaceMembers, $interfaceMember);
        }
    }
    return \@interfaceMembers;
}

sub parseInterfaceMember
{
    my $self = shift;
    my $extendedAttributeList = shift;

    my $next = $self->nextToken();
    if ($next->value() eq "const") {
        return $self->parseConst($extendedAttributeList);
    }
    if ($next->type() == IdentifierToken || $next->value() =~ /$nextInterfaceMember_1/) {
        return $self->parseAttributeOrOperationOrIterator($extendedAttributeList);
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseDictionary
{
    my $self = shift;
    my $extendedAttributeList = shift;

    my $next = $self->nextToken();
    if ($next->value() eq "dictionary") {
        $self->assertTokenValue($self->getToken(), "dictionary", __LINE__);

        my $dictionary = IDLDictionary->new();

        $self->assertExtendedAttributesValidForContext($extendedAttributeList, "dictionary");
        $dictionary->extendedAttributes($extendedAttributeList);

        my $nameToken = $self->getToken();
        $self->assertTokenType($nameToken, IdentifierToken);

        my $name = $nameToken->value();
        $dictionary->type(makeSimpleType($name));

        $next = $self->nextToken();
        if ($next->value() eq ":") {
            my $parent = $self->parseInheritance();
            $dictionary->parentType(makeSimpleType($parent));
        }
        
        $self->assertTokenValue($self->getToken(), "{", __LINE__);
        $dictionary->members($self->parseDictionaryMembers());
        $self->assertTokenValue($self->getToken(), "}", __LINE__);
        $self->assertTokenValue($self->getToken(), ";", __LINE__);
        return $dictionary;
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseDictionaryMembers
{
    my $self = shift;

    my @members = ();

    while (1) {
        my $extendedAttributeList = $self->parseExtendedAttributeListAllowEmpty();
        my $next = $self->nextToken();
        if ($next->type() == IdentifierToken || $next->value() =~ /$nextExceptionField_1/) {
            push(@members, $self->parseDictionaryMember($extendedAttributeList));
        } else {
            last;
        }
    }

    return \@members;
}

sub parseDictionaryMember
{
    my $self = shift;
    my $extendedAttributeList = shift;

    my $next = $self->nextToken();
    if ($next->type() == IdentifierToken || $next->value() =~ /$nextExceptionField_1/) {
        my $member = IDLDictionaryMember->new();

        if ($next->value eq "required") {
            $self->assertTokenValue($self->getToken(), "required", __LINE__);
            $member->isRequired(1);

            my $type = $self->parseTypeWithExtendedAttributes();
            $member->type($type);
        } else {
            $member->isRequired(0);

            my $type = $self->parseType();
            $self->moveExtendedAttributesApplicableToTypes($type, $extendedAttributeList);
            
            $member->type($type);
        }

        $self->assertExtendedAttributesValidForContext($extendedAttributeList, "dictionary-member");
        $member->extendedAttributes($extendedAttributeList);

        my $nameToken = $self->getToken();
        $self->assertTokenType($nameToken, IdentifierToken);
        $member->name($nameToken->value);
        $member->default($self->parseDefault());
        $self->assertTokenValue($self->getToken(), ";", __LINE__);
        return $member;
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parsePartialDictionary
{
    my $self = shift;
    my $next = $self->nextToken();
    if ($next->value() eq "dictionary") {
        $self->assertTokenValue($self->getToken(), "dictionary", __LINE__);
        $self->assertTokenType($self->getToken(), IdentifierToken);
        $self->assertTokenValue($self->getToken(), "{", __LINE__);
        $self->parseDictionaryMembers();
        $self->assertTokenValue($self->getToken(), "}", __LINE__);
        $self->assertTokenValue($self->getToken(), ";", __LINE__);
        return;
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseDefault
{
    my $self = shift;
    my $next = $self->nextToken();
    if ($next->value() eq "=") {
        $self->assertTokenValue($self->getToken(), "=", __LINE__);
        return $self->parseDefaultValue();
    }
    return undef;
}

sub parseDefaultValue
{
    my $self = shift;
    my $next = $self->nextToken();
    if ($next->type() == FloatToken || $next->type() == IntegerToken || $next->value() =~ /$nextDefaultValue_1/) {
        return $self->parseConstValue();
    }
    if ($next->type() == StringToken) {
        return $self->getToken()->value();
    }
    if ($next->value() eq "[") {
        $self->assertTokenValue($self->getToken(), "[", __LINE__);
        $self->assertTokenValue($self->getToken(), "]", __LINE__);
        return "[]";
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseException
{
    my $self = shift;
    my $extendedAttributeList = shift;

    my $next = $self->nextToken();
    if ($next->value() eq "exception") {
        my $interface = IDLInterface->new();
        $self->assertTokenValue($self->getToken(), "exception", __LINE__);
        my $exceptionNameToken = $self->getToken();
        $self->assertTokenType($exceptionNameToken, IdentifierToken);

        my $name = identifierRemoveNullablePrefix($exceptionNameToken->value());
        $interface->type(makeSimpleType($name));
        $interface->isException(1);

        $next = $self->nextToken();
        if ($next->value() eq ":") {
            my $parent = $self->parseInheritance();
            $interface->parentType(makeSimpleType($parent));
        }
        
        $self->assertTokenValue($self->getToken(), "{", __LINE__);
        my $exceptionMembers = $self->parseExceptionMembers();
        $self->assertTokenValue($self->getToken(), "}", __LINE__);
        $self->assertTokenValue($self->getToken(), ";", __LINE__);
        applyMemberList($interface, $exceptionMembers);
        
        $self->assertExtendedAttributesValidForContext($extendedAttributeList, "interface");
        applyExtendedAttributeList($interface, $extendedAttributeList);

        return $interface;
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseExceptionMembers
{
    my $self = shift;
    my @members = ();

    while (1) {
        my $next = $self->nextToken();
        if ($next->type() == IdentifierToken || $next->value() =~ /$nextExceptionMembers_1/) {
            my $extendedAttributeList = $self->parseExtendedAttributeListAllowEmpty();
            #my $member = $self->parseExceptionMember($extendedAttributeList);
            my $member = $self->parseInterfaceMember($extendedAttributeList);
            if (defined ($member)) {
                push(@members, $member);
            }
        } else {
            last;
        }
    }
    return \@members;
}

sub parseInheritance
{
    my $self = shift;

    my $next = $self->nextToken();
    if ($next->value() eq ":") {
        $self->assertTokenValue($self->getToken(), ":", __LINE__);
        return $self->parseName();
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseEnum
{
    my $self = shift;
    my $extendedAttributeList = shift;

    my $next = $self->nextToken();
    if ($next->value() eq "enum") {
        my $enum = IDLEnum->new();
        $self->assertTokenValue($self->getToken(), "enum", __LINE__);
        my $enumNameToken = $self->getToken();
        $self->assertTokenType($enumNameToken, IdentifierToken);
        my $name = identifierRemoveNullablePrefix($enumNameToken->value());
        $enum->name($name);
        $enum->type(makeSimpleType($name));
        $self->assertTokenValue($self->getToken(), "{", __LINE__);
        push(@{$enum->values}, @{$self->parseEnumValueList()});
        $self->assertTokenValue($self->getToken(), "}", __LINE__);
        $self->assertTokenValue($self->getToken(), ";", __LINE__);
        
        $self->assertExtendedAttributesValidForContext($extendedAttributeList, "enum");
        $enum->extendedAttributes($extendedAttributeList);
        return $enum;
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseEnumValueList
{
    my $self = shift;
    my @values = ();
    my $next = $self->nextToken();
    if ($next->type() == StringToken) {
        my $enumValueToken = $self->getToken();
        $self->assertTokenType($enumValueToken, StringToken);
        my $enumValue = $self->unquoteString($enumValueToken->value());
        push(@values, $enumValue);
        push(@values, @{$self->parseEnumValues()});
        return \@values;
    }
    # value list must be non-empty
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseEnumValues
{
    my $self = shift;
    my @values = ();
    my $next = $self->nextToken();
    if ($next->value() eq ",") {
        $self->assertTokenValue($self->getToken(), ",", __LINE__);
        my $enumValueToken = $self->getToken();
        $self->assertTokenType($enumValueToken, StringToken);
        my $enumValue = $self->unquoteString($enumValueToken->value());
        push(@values, $enumValue);
        push(@values, @{$self->parseEnumValues()});
        return \@values;
    }
    return \@values; # empty list (end of enumeration-values)
}

sub parseCallbackRest
{
    my $self = shift;
    my $extendedAttributeList = shift;

    my $next = $self->nextToken();
    if ($next->type() == IdentifierToken) {
        my $callback = IDLCallbackFunction->new();

        my $nameToken = $self->getToken();
        $self->assertTokenType($nameToken, IdentifierToken);

        $callback->type(makeSimpleType($nameToken->value()));

        $self->assertTokenValue($self->getToken(), "=", __LINE__);

        my $operation = IDLOperation->new();
        $operation->type($self->parseReturnType());
        
        $self->assertExtendedAttributesValidForContext($extendedAttributeList, "callback-function", "operation");
        $operation->extendedAttributes($extendedAttributeList);

        $self->assertTokenValue($self->getToken(), "(", __LINE__);

        push(@{$operation->arguments}, @{$self->parseArgumentList()});

        $self->assertTokenValue($self->getToken(), ")", __LINE__);
        $self->assertTokenValue($self->getToken(), ";", __LINE__);

        $callback->operation($operation);
        $callback->extendedAttributes($extendedAttributeList);

        return $callback;
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseTypedef
{
    my $self = shift;
    my $extendedAttributeList = shift;
    die "Extended attributes are not applicable to typedefs themselves: " . $self->{Line} if %{$extendedAttributeList};

    my $next = $self->nextToken();
    if ($next->value() eq "typedef") {
        $self->assertTokenValue($self->getToken(), "typedef", __LINE__);
        my $typedef = IDLTypedef->new();

        my $type = $self->parseTypeWithExtendedAttributes();
        $typedef->type($type);

        my $nameToken = $self->getToken();
        $self->assertTokenType($nameToken, IdentifierToken);
        $self->assertTokenValue($self->getToken(), ";", __LINE__);
        my $name = $nameToken->value();
        die "typedef redefinition for " . $name . " at " . $self->{Line} if (exists $typedefs{$name} && $typedef->type->name ne $typedefs{$name}->type->name);
        $typedefs{$name} = $typedef;
        return;
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseImplementsStatement
{
    my $self = shift;
    my $extendedAttributeList = shift;

    my $next = $self->nextToken();
    if ($next->type() == IdentifierToken) {
        $self->parseName();
        $self->assertTokenValue($self->getToken(), "implements", __LINE__);
        $self->parseName();
        $self->assertTokenValue($self->getToken(), ";", __LINE__);
        return;
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseConst
{
    my $self = shift;
    my $extendedAttributeList = shift;

    my $next = $self->nextToken();
    if ($next->value() eq "const") {
        my $newDataNode = IDLConstant->new();
        $self->assertTokenValue($self->getToken(), "const", __LINE__);
        my $type = $self->parseConstType();
        $newDataNode->type($type);
        my $constNameToken = $self->getToken();
        $self->assertTokenType($constNameToken, IdentifierToken);
        $newDataNode->name(identifierRemoveNullablePrefix($constNameToken->value()));
        $self->assertTokenValue($self->getToken(), "=", __LINE__);
        $newDataNode->value($self->parseConstValue());
        $self->assertTokenValue($self->getToken(), ";", __LINE__);

        $self->assertExtendedAttributesValidForContext($extendedAttributeList, "constant");
        $newDataNode->extendedAttributes($extendedAttributeList);

        return $newDataNode;
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseConstValue
{
    my $self = shift;
    my $next = $self->nextToken();
    if ($next->value() =~ /$nextConstValue_1/) {
        return $self->parseBooleanLiteral();
    }
    if ($next->value() eq "null") {
        $self->assertTokenValue($self->getToken(), "null", __LINE__);
        return "null";
    }
    if ($next->type() == FloatToken || $next->value() =~ /$nextConstValue_2/) {
        return $self->parseFloatLiteral();
    }
    if ($next->type() == IntegerToken) {
        return $self->getToken()->value();
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseBooleanLiteral
{
    my $self = shift;
    my $next = $self->nextToken();
    if ($next->value() eq "true") {
        $self->assertTokenValue($self->getToken(), "true", __LINE__);
        return "true";
    }
    if ($next->value() eq "false") {
        $self->assertTokenValue($self->getToken(), "false", __LINE__);
        return "false";
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseFloatLiteral
{
    my $self = shift;
    my $next = $self->nextToken();
    if ($next->value() eq "-") {
        $self->assertTokenValue($self->getToken(), "-", __LINE__);
        $self->assertTokenValue($self->getToken(), "Infinity", __LINE__);
        return "-Infinity";
    }
    if ($next->value() eq "Infinity") {
        $self->assertTokenValue($self->getToken(), "Infinity", __LINE__);
        return "Infinity";
    }
    if ($next->value() eq "NaN") {
        $self->assertTokenValue($self->getToken(), "NaN", __LINE__);
        return "NaN";
    }
    if ($next->type() == FloatToken) {
        return $self->getToken()->value();
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseAttributeOrOperationOrIterator
{
    my $self = shift;
    my $extendedAttributeList = shift;

    my $next = $self->nextToken();
    if ($next->value() eq "serializer") {
        return $self->parseSerializer($extendedAttributeList);
    }

    if ($next->value() =~ /$nextAttributeOrOperation_1/) {
        my $qualifier = $self->parseQualifier();
        my $isReadOnly = $self->parseReadOnly();
        my $newDataNode = $self->parseAttributeOrOperationRest($extendedAttributeList, $isReadOnly);
        if (defined($newDataNode)) {
            $newDataNode->isStatic(1) if $qualifier eq "static";
            $newDataNode->isStringifier(1) if $qualifier eq "stringifier";
        }
        return $newDataNode;
    }
    my $isReadOnly = $self->parseReadOnly();
    $next = $self->nextToken();
    if ($next->value() =~ /$nextAttribute_1/) {
        return $self->parseAttribute($extendedAttributeList, $isReadOnly);
    }
    if ($next->type() == IdentifierToken || $next->value() =~ /$nextAttributeOrOperation_2/) {
        return $self->parseOperationOrIterator($extendedAttributeList, $isReadOnly);
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseSerializer
{
    my $self = shift;
    my $extendedAttributeList = shift;

    my $next = $self->nextToken();
    if ($next->value() eq "serializer") {
        $self->assertTokenValue($self->getToken(), "serializer", __LINE__);
        my $next = $self->nextToken();
        my $newDataNode;
        if ($next->value() ne ";") {
            $newDataNode = $self->parseSerializerRest($extendedAttributeList);
            my $next = $self->nextToken();
        } else {
            $newDataNode = IDLSerializable->new();
        }

        my $toJSONFunction = IDLOperation->new();
        $toJSONFunction->name("toJSON");

        $self->assertExtendedAttributesValidForContext($extendedAttributeList, "operation");
        $toJSONFunction->extendedAttributes($extendedAttributeList);
        $toJSONFunction->isSerializer(1);
        push(@{$newDataNode->functions}, $toJSONFunction);

        $self->assertTokenValue($self->getToken(), ";", __LINE__);
        return $newDataNode;
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseSerializerRest
{
    my $self = shift;
    my $extendedAttributeList = shift;

    my $next = $self->nextToken();
    if ($next->value() eq "=") {
        $self->assertTokenValue($self->getToken(), "=", __LINE__);

        return $self->parseSerializationPattern();

    }
    if ($next->type() == IdentifierToken || $next->value() eq "(") {
        return $self->parseOperationRest($extendedAttributeList);
    }
}

sub parseSerializationPattern
{
    my $self = shift;

    my $next = $self->nextToken();
    if ($next->value() eq "{") {
        $self->assertTokenValue($self->getToken(), "{", __LINE__);
        my $newDataNode = IDLSerializable->new();
        $self->parseSerializationAttributes($newDataNode);
        $self->assertTokenValue($self->getToken(), "}", __LINE__);
        return $newDataNode;
    }
    if ($next->value() eq "[") {
        die "Serialization of lists pattern is not currently supported.";
    }
    if ($next->type() == IdentifierToken) {
        my @attributes = ();
        my $token = $self->getToken();
        $self->assertTokenType($token, IdentifierToken);
        push(@attributes, $token->value());

        my $newDataNode = IDLSerializable->new();
        $newDataNode->attributes(\@attributes);

        return $newDataNode;
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseSerializationAttributes
{
    my $self = shift;
    my $serializable = shift;

    my @attributes = ();
    my @identifiers = $self->parseIdentifierList();

    for my $identifier (@identifiers) {
        if ($identifier eq "getter") {
            $serializable->hasGetter(1);
            die "Serializer getter keyword is not currently supported.";
        }

        if ($identifier eq "inherit") {
            $serializable->hasInherit(1);
            next;
        }

        if ($identifier eq "attribute") {
            $serializable->hasAttribute(1);
            # Attributes will be filled in via applyMemberList()
            next;
        }

        push(@attributes, $identifier);
    }

    $serializable->attributes(\@attributes);
}

sub parseIdentifierList
{
    my $self = shift;
    my $next = $self->nextToken();

    my @identifiers = ();
    if ($next->type == IdentifierToken) {
        push(@identifiers, $self->getToken()->value());
        push(@identifiers, @{$self->parseIdentifiers()});
    }
    return @identifiers;
}

sub parseIdentifiers
{
    my $self = shift;
    my @idents = ();

    while (1) {
        my $next = $self->nextToken();
        if ($next->value() eq ",") {
            $self->assertTokenValue($self->getToken(), ",", __LINE__);
            my $token = $self->getToken();
            $self->assertTokenType($token, IdentifierToken);
            push(@idents, $token->value());
        } else {
            last;
        }
    }
    return \@idents;
}

sub parseQualifier
{
    my $self = shift;

    my $next = $self->nextToken();
    if ($next->value() eq "static") {
        $self->assertTokenValue($self->getToken(), "static", __LINE__);
        return "static";
    }
    if ($next->value() eq "stringifier") {
        $self->assertTokenValue($self->getToken(), "stringifier", __LINE__);
        return "stringifier";
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseAttributeOrOperationRest
{
    my $self = shift;
    my $extendedAttributeList = shift;
    my $isReadOnly = shift;

    my $next = $self->nextToken();
    if ($next->value() eq "attribute") {
        return $self->parseAttributeRest($extendedAttributeList, $isReadOnly);
    }
    if ($next->value() eq ";") {
        $self->assertTokenValue($self->getToken(), ";", __LINE__);
        return;
    }
    if ($next->type() == IdentifierToken || $next->value() =~ /$nextAttributeOrOperationRest_1/) {
        my $returnType = $self->parseReturnType();

        # NOTE: This is a non-standard addition. In WebIDL, there is no way to associate
        # extended attributes with a return type.
        $self->moveExtendedAttributesApplicableToTypes($returnType, $extendedAttributeList);

        my $operation = $self->parseOperationRest($extendedAttributeList);
        if (defined ($operation)) {
            $operation->type($returnType);
        }
        return $operation;
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseAttribute
{
    my $self = shift;
    my $extendedAttributeList = shift;
    my $isReadOnly = shift;

    my $next = $self->nextToken();
    if ($next->value() =~ /$nextAttribute_1/) {
        $self->parseInherit();
        return $self->parseAttributeRest($extendedAttributeList, $isReadOnly);
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseAttributeRest
{
    my $self = shift;
    my $extendedAttributeList = shift;
    my $isReadOnly = shift;

    my $next = $self->nextToken();
    if ($next->value() eq "attribute") {
        $self->assertTokenValue($self->getToken(), "attribute", __LINE__);

        my $attribute = IDLAttribute->new();
        
        $self->assertExtendedAttributesValidForContext($extendedAttributeList, "attribute");
        $attribute->extendedAttributes($extendedAttributeList);
        $attribute->isReadOnly($isReadOnly);

        my $type = $self->parseTypeWithExtendedAttributes();
        $attribute->type($type);

        my $token = $self->getToken();
        $self->assertTokenType($token, IdentifierToken);
        $attribute->name(identifierRemoveNullablePrefix($token->value()));
        $self->assertTokenValue($self->getToken(), ";", __LINE__);

        return $attribute;
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseInherit
{
    my $self = shift;
    my $next = $self->nextToken();
    if ($next->value() eq "inherit") {
        $self->assertTokenValue($self->getToken(), "inherit", __LINE__);
        return 1;
    }
    return 0;
}

sub parseReadOnly
{
    my $self = shift;
    my $next = $self->nextToken();
    if ($next->value() eq "readonly") {
        $self->assertTokenValue($self->getToken(), "readonly", __LINE__);
        return 1;
    }
    return 0;
}

sub parseOperationOrIterator
{
    my $self = shift;
    my $extendedAttributeList = shift;
    my $isReadOnly = shift;

    my $next = $self->nextToken();
    if ($next->value() =~ /$nextSpecials_1/) {
        return $self->parseSpecialOperation($extendedAttributeList);
    }
    if ($next->value() eq "iterable") {
        return $self->parseIterableRest($extendedAttributeList);
    }
    if ($next->value() eq "maplike") {
        return $self->parseMapLikeRest($extendedAttributeList, $isReadOnly);
    }
    if ($next->type() == IdentifierToken || $next->value() =~ /$nextAttributeOrOperationRest_1/) {
        my $returnType = $self->parseReturnType();
        # NOTE: This is a non-standard addition. In WebIDL, there is no way to associate
        # extended attributes with a return type.
        $self->moveExtendedAttributesApplicableToTypes($returnType, $extendedAttributeList);

        my $next = $self->nextToken();
        if ($next->type() == IdentifierToken || $next->value() eq "(") {
            my $operation = $self->parseOperationRest($extendedAttributeList);
            $operation->type($returnType);
            return $operation;
        }
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseSpecialOperation
{
    my $self = shift;
    my $extendedAttributeList = shift;

    my $next = $self->nextToken();
    if ($next->value() =~ /$nextSpecials_1/) {
        my @specials = ();
        push(@specials, @{$self->parseSpecials()});
        my $returnType = $self->parseReturnType();

        # NOTE: This is a non-standard addition. In WebIDL, there is no way to associate
        # extended attributes with a return type.
        $self->moveExtendedAttributesApplicableToTypes($returnType, $extendedAttributeList);

        my $operation = $self->parseOperationRest($extendedAttributeList);
        if (defined ($operation)) {
            $operation->type($returnType);
            $operation->specials(\@specials);
        }
        return $operation;
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseSpecials
{
    my $self = shift;
    my @specials = ();

    while (1) {
        my $next = $self->nextToken();
        if ($next->value() =~ /$nextSpecials_1/) {
            push(@specials, $self->parseSpecial());
        } else {
            last;
        }
    }
    return \@specials;
}

sub parseSpecial
{
    my $self = shift;
    my $next = $self->nextToken();
    if ($next->value() eq "getter") {
        $self->assertTokenValue($self->getToken(), "getter", __LINE__);
        return "getter";
    }
    if ($next->value() eq "setter") {
        $self->assertTokenValue($self->getToken(), "setter", __LINE__);
        return "setter";
    }
    if ($next->value() eq "creator") {
        $self->assertTokenValue($self->getToken(), "creator", __LINE__);
        return "creator";
    }
    if ($next->value() eq "deleter") {
        $self->assertTokenValue($self->getToken(), "deleter", __LINE__);
        return "deleter";
    }
    if ($next->value() eq "legacycaller") {
        $self->assertTokenValue($self->getToken(), "legacycaller", __LINE__);
        return "legacycaller";
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseIterableRest
{
    my $self = shift;
    my $extendedAttributeList = shift;

    my $next = $self->nextToken();
    if ($next->value() eq "iterable") {
        $self->assertTokenValue($self->getToken(), "iterable", __LINE__);
        my $iterableNode = $self->parseOptionalIterableInterface($extendedAttributeList);
        $self->assertTokenValue($self->getToken(), ";", __LINE__);
        return $iterableNode;
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseOptionalIterableInterface
{
    my $self = shift;
    my $extendedAttributeList = shift;

    my $newDataNode = IDLIterable->new();

    $self->assertExtendedAttributesValidForContext($extendedAttributeList, "iterable");
    $newDataNode->extendedAttributes($extendedAttributeList);

    $self->assertTokenValue($self->getToken(), "<", __LINE__);
    my $type1 = $self->parseTypeWithExtendedAttributes();

    if ($self->nextToken()->value() eq ",") {
        $self->assertTokenValue($self->getToken(), ",", __LINE__);

        my $type2 = $self->parseTypeWithExtendedAttributes();
        $newDataNode->isKeyValue(1);
        $newDataNode->keyType($type1);
        $newDataNode->valueType($type2);
    } else {
        $newDataNode->isKeyValue(0);
        $newDataNode->valueType($type1);
    }
    $self->assertTokenValue($self->getToken(), ">", __LINE__);

    my $symbolIteratorFunction = IDLOperation->new();
    $symbolIteratorFunction->name("[Symbol.Iterator]");
    $symbolIteratorFunction->extendedAttributes($extendedAttributeList);
    $symbolIteratorFunction->isIterable(1);

    my $entriesFunction = IDLOperation->new();
    $entriesFunction->name("entries");
    $entriesFunction->extendedAttributes($extendedAttributeList);
    $entriesFunction->isIterable(1);

    my $keysFunction = IDLOperation->new();
    $keysFunction->name("keys");
    $keysFunction->extendedAttributes($extendedAttributeList);
    $keysFunction->isIterable(1);

    my $valuesFunction = IDLOperation->new();
    $valuesFunction->name("values");
    $valuesFunction->extendedAttributes($extendedAttributeList);
    $valuesFunction->isIterable(1);

    my $forEachFunction = IDLOperation->new();
    $forEachFunction->name("forEach");
    $forEachFunction->extendedAttributes($extendedAttributeList);
    $forEachFunction->isIterable(1);
    my $forEachArgument = IDLArgument->new();
    $forEachArgument->name("callback");
    $forEachArgument->type(makeSimpleType("any"));
    push(@{$forEachFunction->arguments}, ($forEachArgument));

    push(@{$newDataNode->functions}, $symbolIteratorFunction);
    push(@{$newDataNode->functions}, $entriesFunction);
    push(@{$newDataNode->functions}, $keysFunction);
    push(@{$newDataNode->functions}, $valuesFunction);
    push(@{$newDataNode->functions}, $forEachFunction);

    return $newDataNode;
}

sub parseMapLikeRest
{
    my $self = shift;
    my $extendedAttributeList = shift;
    my $isReadOnly = shift;

    my $next = $self->nextToken();
    if ($next->value() eq "maplike") {
        $self->assertTokenValue($self->getToken(), "maplike", __LINE__);
        my $mapLikeNode = $self->parseMapLikeProperties($extendedAttributeList, $isReadOnly);
        $self->assertTokenValue($self->getToken(), ";", __LINE__);
        return $mapLikeNode;
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseMapLikeProperties
{
    my $self = shift;
    my $extendedAttributeList = shift;
    my $isReadOnly = shift;

    my $newDataNode = IDLMapLike->new();
    $newDataNode->extendedAttributes($extendedAttributeList);
    $newDataNode->isReadOnly($isReadOnly);

    $self->assertTokenValue($self->getToken(), "<", __LINE__);
    $newDataNode->keyType($self->parseTypeWithExtendedAttributes());
    $self->assertTokenValue($self->getToken(), ",", __LINE__);
    $newDataNode->valueType($self->parseTypeWithExtendedAttributes());
    $self->assertTokenValue($self->getToken(), ">", __LINE__);

    my $notEnumerableExtendedAttributeList = $extendedAttributeList;
    $notEnumerableExtendedAttributeList->{NotEnumerable} = 1;

    my $sizeAttribute = IDLAttribute->new();
    $sizeAttribute->name("size");
    $sizeAttribute->isMapLike(1);
    $sizeAttribute->extendedAttributes($extendedAttributeList);
    $sizeAttribute->isReadOnly(1);
    $sizeAttribute->type(makeSimpleType("any"));
    push(@{$newDataNode->attributes}, $sizeAttribute);

    my $getFunction = IDLOperation->new();
    $getFunction->name("get");
    $getFunction->isMapLike(1);
    my $getArgument = IDLArgument->new();
    $getArgument->name("key");
    $getArgument->type($newDataNode->keyType);
    $getArgument->extendedAttributes($extendedAttributeList);
    push(@{$getFunction->arguments}, ($getArgument));
    $getFunction->extendedAttributes($notEnumerableExtendedAttributeList);
    $getFunction->type(makeSimpleType("any"));

    my $hasFunction = IDLOperation->new();
    $hasFunction->name("has");
    $hasFunction->isMapLike(1);
    my $hasArgument = IDLArgument->new();
    $hasArgument->name("key");
    $hasArgument->type($newDataNode->keyType);
    $hasArgument->extendedAttributes($extendedAttributeList);
    push(@{$hasFunction->arguments}, ($hasArgument));
    $hasFunction->extendedAttributes($notEnumerableExtendedAttributeList);
    $hasFunction->type(makeSimpleType("any"));

    my $entriesFunction = IDLOperation->new();
    $entriesFunction->name("entries");
    $entriesFunction->isMapLike(1);
    $entriesFunction->extendedAttributes($notEnumerableExtendedAttributeList);
    $entriesFunction->type(makeSimpleType("any"));

    my $keysFunction = IDLOperation->new();
    $keysFunction->name("keys");
    $keysFunction->isMapLike(1);
    $keysFunction->extendedAttributes($notEnumerableExtendedAttributeList);
    $keysFunction->type(makeSimpleType("any"));

    my $valuesFunction = IDLOperation->new();
    $valuesFunction->name("values");
    $valuesFunction->isMapLike(1);
    $valuesFunction->extendedAttributes($extendedAttributeList);
    $valuesFunction->extendedAttributes->{NotEnumerable} = 1;
    $valuesFunction->type(makeSimpleType("any"));

    my $forEachFunction = IDLOperation->new();
    $forEachFunction->name("forEach");
    $forEachFunction->isMapLike(1);
    $forEachFunction->extendedAttributes({});
    $forEachFunction->type(makeSimpleType("any"));
    my $forEachArgument = IDLArgument->new();
    $forEachArgument->name("callback");
    $forEachArgument->type(makeSimpleType("any"));
    $forEachArgument->extendedAttributes($extendedAttributeList);
    push(@{$forEachFunction->arguments}, ($forEachArgument));

    push(@{$newDataNode->functions}, $getFunction);
    push(@{$newDataNode->functions}, $hasFunction);
    push(@{$newDataNode->functions}, $entriesFunction);
    push(@{$newDataNode->functions}, $keysFunction);
    push(@{$newDataNode->functions}, $valuesFunction);
    push(@{$newDataNode->functions}, $forEachFunction);

    return $newDataNode if $isReadOnly;

    my $addFunction = IDLOperation->new();
    $addFunction->name("add");
    $addFunction->isMapLike(1);
    my $addArgument = IDLArgument->new();
    $addArgument->name("key");
    $addArgument->type($newDataNode->keyType);
    $addArgument->extendedAttributes($extendedAttributeList);
    push(@{$addFunction->arguments}, ($addArgument));
    $addFunction->extendedAttributes($notEnumerableExtendedAttributeList);
    $addFunction->type(makeSimpleType("any"));

    my $clearFunction = IDLOperation->new();
    $clearFunction->name("clear");
    $clearFunction->isMapLike(1);
    $clearFunction->extendedAttributes($notEnumerableExtendedAttributeList);
    $clearFunction->type(makeSimpleType("void"));

    my $deleteFunction = IDLOperation->new();
    $deleteFunction->name("delete");
    $deleteFunction->isMapLike(1);
    my $deleteArgument = IDLArgument->new();
    $deleteArgument->name("key");
    $deleteArgument->type($newDataNode->keyType);
    $deleteArgument->extendedAttributes($extendedAttributeList);
    push(@{$deleteFunction->arguments}, ($deleteArgument));
    $deleteFunction->extendedAttributes($notEnumerableExtendedAttributeList);
    $deleteFunction->type(makeSimpleType("any"));

    push(@{$newDataNode->functions}, $addFunction);
    push(@{$newDataNode->functions}, $clearFunction);
    push(@{$newDataNode->functions}, $deleteFunction);

    return $newDataNode;
}

sub parseOperationRest
{
    my $self = shift;
    my $extendedAttributeList = shift;

    my $next = $self->nextToken();
    if ($next->type() == IdentifierToken || $next->value() eq "(") {
        my $newDataNode = IDLOperation->new();

        my $name = $self->parseOptionalIdentifier();
        $newDataNode->name(identifierRemoveNullablePrefix($name));

        $self->assertTokenValue($self->getToken(), "(", $name, __LINE__);

        push(@{$newDataNode->arguments}, @{$self->parseArgumentList()});

        $self->assertTokenValue($self->getToken(), ")", __LINE__);
        $self->assertTokenValue($self->getToken(), ";", __LINE__);

        $self->assertExtendedAttributesValidForContext($extendedAttributeList, "operation");
        $newDataNode->extendedAttributes($extendedAttributeList);
        return $newDataNode;
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseOptionalIdentifier
{
    my $self = shift;
    my $next = $self->nextToken();
    if ($next->type() == IdentifierToken) {
        my $token = $self->getToken();
        return $token->value();
    }
    return "";
}

sub parseArgumentList
{
    my $self = shift;
    my @arguments = ();

    my $next = $self->nextToken();
    if ($next->type() == IdentifierToken || $next->value() =~ /$nextArgumentList_1/) {
        push(@arguments, $self->parseArgument());
        push(@arguments, @{$self->parseArguments()});
    }
    return \@arguments;
}

sub parseArguments
{
    my $self = shift;
    my @arguments = ();

    while (1) {
        my $next = $self->nextToken();
        if ($next->value() eq ",") {
            $self->assertTokenValue($self->getToken(), ",", __LINE__);
            push(@arguments, $self->parseArgument());
        } else {
            last;
        }
    }
    return \@arguments;
}

sub parseArgument
{
    my $self = shift;
    my $next = $self->nextToken();
    if ($next->type() == IdentifierToken || $next->value() =~ /$nextArgumentList_1/) {
        my $extendedAttributeList = $self->parseExtendedAttributeListAllowEmpty();
        my $argument = $self->parseOptionalOrRequiredArgument($extendedAttributeList);
        return $argument;
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseOptionalOrRequiredArgument
{
    my $self = shift;
    my $extendedAttributeList = shift;

    my $argument = IDLArgument->new();

    my $next = $self->nextToken();
    if ($next->value() eq "optional") {
        $self->assertTokenValue($self->getToken(), "optional", __LINE__);

        my $type = $self->parseTypeWithExtendedAttributes();
        $argument->type($type);
        $argument->isOptional(1);
        $argument->name($self->parseArgumentName());
        $argument->default($self->parseDefault());

        $self->assertExtendedAttributesValidForContext($extendedAttributeList, "argument");
        $argument->extendedAttributes($extendedAttributeList);

        return $argument;
    }
    if ($next->type() == IdentifierToken || $next->value() =~ /$nextExceptionField_1/) {
        my $type = $self->parseType();
        $self->moveExtendedAttributesApplicableToTypes($type, $extendedAttributeList);

        $argument->type($type);
        $argument->isOptional(0);
        $argument->isVariadic($self->parseEllipsis());
        $argument->name($self->parseArgumentName());

        $self->assertExtendedAttributesValidForContext($extendedAttributeList, "argument");
        $argument->extendedAttributes($extendedAttributeList);

        return $argument;
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseArgumentName
{
    my $self = shift;
    my $next = $self->nextToken();
    if ($next->value() =~ /$nextArgumentName_1/) {
        return $self->parseArgumentNameKeyword();
    }
    if ($next->type() == IdentifierToken) {
        return $self->getToken()->value();
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseEllipsis
{
    my $self = shift;
    my $next = $self->nextToken();
    if ($next->value() eq "...") {
        $self->assertTokenValue($self->getToken(), "...", __LINE__);
        return 1;
    }
    return 0;
}

sub parseExceptionMember
{
    my $self = shift;
    my $extendedAttributeList = shift;

    my $next = $self->nextToken();
    if ($next->value() eq "const") {
        return $self->parseConst($extendedAttributeList);
    }
    if ($next->type() == IdentifierToken || $next->value() =~ /$nextExceptionField_1/) {
        return $self->parseExceptionField($extendedAttributeList);
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseExceptionField
{
    my $self = shift;
    my $extendedAttributeList = shift;

    my $next = $self->nextToken();
    if ($next->type() == IdentifierToken || $next->value() =~ /$nextExceptionField_1/) {
        my $newDataNode = IDLAttribute->new();
        $newDataNode->isReadOnly(1);

        my $type = $self->parseType();
        $newDataNode->type($type);
        
        my $token = $self->getToken();
        $self->assertTokenType($token, IdentifierToken);
        $newDataNode->name(identifierRemoveNullablePrefix($token->value()));
        $self->assertTokenValue($self->getToken(), ";", __LINE__);
        $newDataNode->extendedAttributes($extendedAttributeList);
        return $newDataNode;
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseExtendedAttributeListAllowEmpty
{
    my $self = shift;
    my $next = $self->nextToken();
    if ($next->value() eq "[") {
        return $self->parseExtendedAttributeList();
    }
    return {};
}

sub parseExtendedAttributeList
{
    my $self = shift;
    my $next = $self->nextToken();
    if ($next->value() eq "[") {
        $self->assertTokenValue($self->getToken(), "[", __LINE__);
        my $extendedAttributeList = {};
        my $attr = $self->parseExtendedAttribute();
        copyExtendedAttributes($extendedAttributeList, $attr);
        $attr = $self->parseExtendedAttributes();
        copyExtendedAttributes($extendedAttributeList, $attr);
        $self->assertTokenValue($self->getToken(), "]", __LINE__);
        return $extendedAttributeList;
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseExtendedAttributes
{
    my $self = shift;
    my $extendedAttributeList = {};

    while (1) {
        my $next = $self->nextToken();
        if ($next->value() eq ",") {
            $self->assertTokenValue($self->getToken(), ",", __LINE__);
            my $attr = $self->parseExtendedAttribute2();
            copyExtendedAttributes($extendedAttributeList, $attr);
        } else {
            last;
        }
    }
    return $extendedAttributeList;
}

sub parseExtendedAttribute
{
    my $self = shift;
    my $next = $self->nextToken();
    if ($next->type() == IdentifierToken) {
        my $name = $self->parseName();
        return $self->parseExtendedAttributeRest($name);
    }
    # backward compatibility. Spec doesn' allow "[]". But WebKit requires.
    if ($next->value() eq ']') {
        return {};
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseExtendedAttribute2
{
    my $self = shift;
    my $next = $self->nextToken();
    if ($next->type() == IdentifierToken) {
        my $name = $self->parseName();
        return $self->parseExtendedAttributeRest($name);
    }
    return {};
}

sub parseExtendedAttributeRest
{
    my $self = shift;
    my $name = shift;
    my $attrs = {};

    my $next = $self->nextToken();
    if ($next->value() eq "(") {
        $self->assertTokenValue($self->getToken(), "(", __LINE__);
        $attrs->{$name} = $self->parseArgumentList();
        $self->assertTokenValue($self->getToken(), ")", __LINE__);
        return $attrs;
    }
    if ($next->value() eq "=") {
        $self->assertTokenValue($self->getToken(), "=", __LINE__);
        $attrs->{$name} = $self->parseExtendedAttributeRest2();
        return $attrs;
    }

    if ($name eq "Constructor" || $name eq "CustomConstructor") {
        $attrs->{$name} = [];
    } else {
        $attrs->{$name} = "VALUE_IS_MISSING";
    }
    return $attrs;
}

sub parseExtendedAttributeRest2
{
    my $self = shift;
    my $next = $self->nextToken();
    if ($next->value() eq "(") {
        $self->assertTokenValue($self->getToken(), "(", __LINE__);
        my @arguments = $self->parseIdentifierList();
        $self->assertTokenValue($self->getToken(), ")", __LINE__);
        return @arguments;
    }
    if ($next->type() == IdentifierToken) {
        my $name = $self->parseName();
        return $self->parseExtendedAttributeRest3($name);
    }
    if ($next->type() == IntegerToken) {
        my $token = $self->getToken();
        return $token->value();
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseExtendedAttributeRest3
{
    my $self = shift;
    my $name = shift;

    my $next = $self->nextToken();
    if ($next->value() eq "&") {
        $self->assertTokenValue($self->getToken(), "&", __LINE__);
        my $rightValue = $self->parseName();
        return $name . "&" . $rightValue;
    }
    if ($next->value() eq "|") {
        $self->assertTokenValue($self->getToken(), "|", __LINE__);
        my $rightValue = $self->parseName();
        return $name . "|" . $rightValue;
    }
    if ($next->value() eq "(") {
        my $attr = {};
        $self->assertTokenValue($self->getToken(), "(", __LINE__);
        $attr->{$name} = $self->parseArgumentList();
        $self->assertTokenValue($self->getToken(), ")", __LINE__);
        return $attr;
    }
    if ($next->type() == IdentifierToken || $next->value() =~ /$nextExtendedAttributeRest3_1/) {
        $self->parseNameNoComma();
        return $name;
    }
    $self->assertUnexpectedToken($next->value());
}

sub parseArgumentNameKeyword
{
    my $self = shift;
    my $next = $self->nextToken();
    if ($next->value() eq "attribute") {
        return $self->getToken()->value();
    }
    if ($next->value() eq "callback") {
        return $self->getToken()->value();
    }
    if ($next->value() eq "const") {
        return $self->getToken()->value();
    }
    if ($next->value() eq "creator") {
        return $self->getToken()->value();
    }
    if ($next->value() eq "deleter") {
        return $self->getToken()->value();
    }
    if ($next->value() eq "dictionary") {
        return $self->getToken()->value();
    }
    if ($next->value() eq "enum") {
        return $self->getToken()->value();
    }
    if ($next->value() eq "exception") {
        return $self->getToken()->value();
    }
    if ($next->value() eq "getter") {
        return $self->getToken()->value();
    }
    if ($next->value() eq "implements") {
        return $self->getToken()->value();
    }
    if ($next->value() eq "inherit") {
        return $self->getToken()->value();
    }
    if ($next->value() eq "interface") {
        return $self->getToken()->value();
    }
    if ($next->value() eq "legacycaller") {
        return $self->getToken()->value();
    }
    if ($next->value() eq "partial") {
        return $self->getToken()->value();
    }
    if ($next->value() eq "serializer") {
        return $self->getToken()->value();
    }
    if ($next->value() eq "setter") {
        return $self->getToken()->value();
    }
    if ($next->value() eq "static") {
        return $self->getToken()->value();
    }
    if ($next->value() eq "stringifier") {
        return $self->getToken()->value();
    }
    if ($next->value() eq "typedef") {
        return $self->getToken()->value();
    }
    if ($next->value() eq "unrestricted") {
        return $self->getToken()->value();
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseType
{
    my $self = shift;
    my $next = $self->nextToken();

    my $extendedAttributeList = {};

    if ($next->value() eq "(") {
        my $unionType = $self->parseUnionType();
        $unionType->isNullable($self->parseNull());
        $unionType->extendedAttributes($extendedAttributeList);
        return $unionType;
    }
    if ($next->type() == IdentifierToken || $next->value() =~ /$nextType_1/) {
        my $singleType = $self->parseSingleType();
        $singleType->extendedAttributes($extendedAttributeList);
        return $singleType;
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseTypeWithExtendedAttributes
{
    my $self = shift;
    
    my $extendedAttributeList = $self->parseExtendedAttributeListAllowEmpty();
    $self->assertExtendedAttributesValidForContext($extendedAttributeList, "type");

    my $next = $self->nextToken();
    if ($next->value() eq "(") {
        my $unionType = $self->parseUnionType();
        $unionType->isNullable($self->parseNull());
        $unionType->extendedAttributes($extendedAttributeList);
        return $unionType;
    }
    if ($next->type() == IdentifierToken || $next->value() =~ /$nextType_1/) {
        my $singleType = $self->parseSingleType();
        $singleType->extendedAttributes($extendedAttributeList);
        return $singleType;
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseSingleType
{
    my $self = shift;
    my $next = $self->nextToken();
    if ($next->value() eq "any") {
        $self->assertTokenValue($self->getToken(), "any", __LINE__);
        
        my $anyType = IDLType->new();
        $anyType->name("any");
        return $anyType;
    }
    if ($next->type() == IdentifierToken || $next->value() =~ /$nextSingleType_1/) {
        my $nonAnyType = $self->parseNonAnyType();
        $nonAnyType->isNullable($self->parseNull());
        return $nonAnyType;
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseUnionType
{
    my $self = shift;
    my $next = $self->nextToken();

    my $unionType = IDLType->new();
    $unionType->name("UNION");
    $unionType->isUnion(1);

    if ($next->value() eq "(") {
        $self->assertTokenValue($self->getToken(), "(", __LINE__);
        
        push(@{$unionType->subtypes}, $self->parseUnionMemberType());
        push(@{$unionType->subtypes}, $self->parseUnionMemberTypes());
        
        $self->assertTokenValue($self->getToken(), ")", __LINE__);

        return $unionType;
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseUnionMemberType
{
    my $self = shift;

    my $extendedAttributeList = $self->parseExtendedAttributeListAllowEmpty();
    $self->assertExtendedAttributesValidForContext($extendedAttributeList, "type");

    my $next = $self->nextToken();

    if ($next->value() eq "(") {
        my $unionType = $self->parseUnionType();
        $unionType->isNullable($self->parseNull());
        $unionType->extendedAttributes($extendedAttributeList);
        return $unionType;
    }

    if ($next->type() == IdentifierToken || $next->value() =~ /$nextSingleType_1/) {
        my $nonAnyType = $self->parseNonAnyType();
        $nonAnyType->isNullable($self->parseNull());
        $nonAnyType->extendedAttributes($extendedAttributeList);
        return $nonAnyType;
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseUnionMemberTypes
{
    my $self = shift;
    my $next = $self->nextToken();

    my @subtypes = ();

    if ($next->value() eq "or") {
        $self->assertTokenValue($self->getToken(), "or", __LINE__);
        push(@subtypes, $self->parseUnionMemberType());
        push(@subtypes, $self->parseUnionMemberTypes());
    }

    return @subtypes;
}

sub parseNonAnyType
{
    my $self = shift;
    my $next = $self->nextToken();

    my $type = IDLType->new();

    if ($next->value() =~ /$nextNonAnyType_1/) {
        $type->name($self->parsePrimitiveType());
        return $type;
    }
    if ($next->value() =~ /$nextStringType_1/) {
        $type->name($self->parseStringType());
        return $type;
    }
    if ($next->value() eq "object") {
        $self->assertTokenValue($self->getToken(), "object", __LINE__);

        $type->name("object");
        return $type;
    }
    if ($next->value() eq "RegExp") {
        $self->assertTokenValue($self->getToken(), "RegExp", __LINE__);

        $type->name("RegExp");
        return $type;
    }
    if ($next->value() eq "Error") {
        $self->assertTokenValue($self->getToken(), "Error", __LINE__);

        $type->name("Error");
        return $type;
    }
    if ($next->value() eq "DOMException") {
        $self->assertTokenValue($self->getToken(), "DOMException", __LINE__);

        $type->name("DOMException");
        return $type;
    }
    if ($next->value() eq "Date") {
        $self->assertTokenValue($self->getToken(), "Date", __LINE__);

        $type->name("Date");
        return $type;
    }
    if ($next->value() eq "sequence") {
        $self->assertTokenValue($self->getToken(), "sequence", __LINE__);
        $self->assertTokenValue($self->getToken(), "<", __LINE__);

        my $subtype = $self->parseTypeWithExtendedAttributes();

        $self->assertTokenValue($self->getToken(), ">", __LINE__);

        $type->name("sequence");
        push(@{$type->subtypes}, $subtype);

        return $type;
    }
    if ($next->value() eq "FrozenArray") {
        $self->assertTokenValue($self->getToken(), "FrozenArray", __LINE__);
        $self->assertTokenValue($self->getToken(), "<", __LINE__);

        my $subtype = $self->parseTypeWithExtendedAttributes();

        $self->assertTokenValue($self->getToken(), ">", __LINE__);

        $type->name("FrozenArray");
        push(@{$type->subtypes}, $subtype);

        return $type;
    }
    if ($next->value() eq "Promise") {
        $self->assertTokenValue($self->getToken(), "Promise", __LINE__);
        $self->assertTokenValue($self->getToken(), "<", __LINE__);

        my $subtype = $self->parseReturnType();

        $self->assertTokenValue($self->getToken(), ">", __LINE__);

        $type->name("Promise");
        push(@{$type->subtypes}, $subtype);

        return $type;
    }
    if ($next->value() eq "record") {
        $self->assertTokenValue($self->getToken(), "record", __LINE__);
        $self->assertTokenValue($self->getToken(), "<", __LINE__);

        my $keyType = IDLType->new();
        $keyType->name($self->parseStringType());

        $self->assertTokenValue($self->getToken(), ",", __LINE__);

        my $valueType = $self->parseTypeWithExtendedAttributes();

        $self->assertTokenValue($self->getToken(), ">", __LINE__);

        $type->name("record");
        push(@{$type->subtypes}, $keyType);
        push(@{$type->subtypes}, $valueType);

        return $type;
    }
    if ($next->type() == IdentifierToken) {
        my $identifier = $self->getToken();

        $type->name(identifierRemoveNullablePrefix($identifier->value()));
        return $type;
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseConstType
{
    my $self = shift;
    my $next = $self->nextToken();

    my $type = IDLType->new();

    if ($next->value() =~ /$nextNonAnyType_1/) {
        $type->name($self->parsePrimitiveType());
        $type->isNullable($self->parseNull());
        return $type;
    }
    if ($next->type() == IdentifierToken) {
        my $identifier = $self->getToken();
        
        $type->name($identifier->value());
        $type->isNullable($self->parseNull());

        return $type;
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseStringType
{
    my $self = shift;
    my $next = $self->nextToken();
    if ($next->value() eq "ByteString") {
        $self->assertTokenValue($self->getToken(), "ByteString", __LINE__);
        return "ByteString";
    }
    if ($next->value() eq "DOMString") {
        $self->assertTokenValue($self->getToken(), "DOMString", __LINE__);
        return "DOMString";
    }
    if ($next->value() eq "USVString") {
        $self->assertTokenValue($self->getToken(), "USVString", __LINE__);
        return "USVString";
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parsePrimitiveType
{
    my $self = shift;
    my $next = $self->nextToken();
    if ($next->value() =~ /$nextPrimitiveType_1/) {
        return $self->parseUnsignedIntegerType();
    }
    if ($next->value() =~ /$nextPrimitiveType_2/) {
        return $self->parseUnrestrictedFloatType();
    }
    if ($next->value() eq "boolean") {
        $self->assertTokenValue($self->getToken(), "boolean", __LINE__);
        return "boolean";
    }
    if ($next->value() eq "byte") {
        $self->assertTokenValue($self->getToken(), "byte", __LINE__);
        return "byte";
    }
    if ($next->value() eq "octet") {
        $self->assertTokenValue($self->getToken(), "octet", __LINE__);
        return "octet";
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseUnrestrictedFloatType
{
    my $self = shift;
    my $next = $self->nextToken();
    if ($next->value() eq "unrestricted") {
        $self->assertTokenValue($self->getToken(), "unrestricted", __LINE__);
        return "unrestricted " . $self->parseFloatType();
    }
    if ($next->value() =~ /$nextUnrestrictedFloatType_1/) {
        return $self->parseFloatType();
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseFloatType
{
    my $self = shift;
    my $next = $self->nextToken();
    if ($next->value() eq "float") {
        $self->assertTokenValue($self->getToken(), "float", __LINE__);
        return "float";
    }
    if ($next->value() eq "double") {
        $self->assertTokenValue($self->getToken(), "double", __LINE__);
        return "double";
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseUnsignedIntegerType
{
    my $self = shift;
    my $next = $self->nextToken();
    if ($next->value() eq "unsigned") {
        $self->assertTokenValue($self->getToken(), "unsigned", __LINE__);
        return "unsigned " . $self->parseIntegerType();
    }
    if ($next->value() =~ /$nextUnsignedIntegerType_1/) {
        return $self->parseIntegerType();
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseIntegerType
{
    my $self = shift;
    my $next = $self->nextToken();
    if ($next->value() eq "short") {
        $self->assertTokenValue($self->getToken(), "short", __LINE__);
        return "short";
    }
    if ($next->value() eq "int") {
        $self->assertTokenValue($self->getToken(), "int", __LINE__);
        return "int";
    }
    if ($next->value() eq "long") {
        $self->assertTokenValue($self->getToken(), "long", __LINE__);
        if ($self->parseOptionalLong()) {
            return "long long";
        }
        return "long";
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseOptionalLong
{
    my $self = shift;
    my $next = $self->nextToken();
    if ($next->value() eq "long") {
        $self->assertTokenValue($self->getToken(), "long", __LINE__);
        return 1;
    }
    return 0;
}

sub parseNull
{
    my $self = shift;
    my $next = $self->nextToken();
    if ($next->value() eq "?") {
        $self->assertTokenValue($self->getToken(), "?", __LINE__);
        return 1;
    }
    return 0;
}

sub parseReturnType
{
    my $self = shift;
    my $next = $self->nextToken();
    if ($next->value() eq "void") {
        $self->assertTokenValue($self->getToken(), "void", __LINE__);
        
        my $voidType = IDLType->new();
        $voidType->name("void");
        return $voidType;
    }
    if ($next->type() == IdentifierToken || $next->value() =~ /$nextExceptionField_1/) {
        return $self->parseType();
    }
    $self->assertUnexpectedToken($next->value(), __LINE__);
}

sub parseOptionalSemicolon
{
    my $self = shift;
    my $next = $self->nextToken();
    if ($next->value() eq ";") {
        $self->assertTokenValue($self->getToken(), ";", __LINE__);
    }
}

sub parseNameNoComma
{
    my $self = shift;
    my $next = $self->nextToken();
    if ($next->type() == IdentifierToken) {
        my $identifier = $self->getToken();
        return ($identifier->value());
    }

    return ();
}

sub parseName
{
    my $self = shift;
    my $next = $self->nextToken();
    if ($next->type() == IdentifierToken) {
        my $identifier = $self->getToken();
        return $identifier->value();
    }
    $self->assertUnexpectedToken($next->value());
}

sub applyMemberList
{
    my $interface = shift;
    my $members = shift;

    for my $item (@{$members}) {
        if (ref($item) eq "IDLAttribute") {
            push(@{$interface->attributes}, $item);
            next;
        }
        if (ref($item) eq "IDLConstant") {
            push(@{$interface->constants}, $item);
            next;
        }
        if (ref($item) eq "IDLIterable") {
            $interface->iterable($item);
            next;
        }
        if (ref($item) eq "IDLMapLike") {
            $interface->mapLike($item);
            next;
        }
        if (ref($item) eq "IDLOperation") {
            if ($item->name eq "") {
                push(@{$interface->anonymousFunctions}, $item);
            } else {
                push(@{$interface->functions}, $item);
            }
            next;
        }
        if (ref($item) eq "IDLSerializable") {
            $interface->serializable($item);
            next;
        }
    }

    if ($interface->serializable) {
        my $numSerializerAttributes = @{$interface->serializable->attributes};
        if ($interface->serializable->hasAttribute) {
            foreach my $attribute (@{$interface->attributes}) {
                push(@{$interface->serializable->attributes}, $attribute->name);
            }
        } elsif ($numSerializerAttributes == 0) {
            foreach my $attribute (@{$interface->attributes}) {
                push(@{$interface->serializable->attributes}, $attribute->name);
            }
        }
    }
}

sub applyExtendedAttributeList
{
    my $interface = shift;
    my $extendedAttributeList = shift;

    if (defined $extendedAttributeList->{"Constructors"}) {
        my @constructorParams = @{$extendedAttributeList->{"Constructors"}};
        my $index = (@constructorParams == 1) ? 0 : 1;
        foreach my $param (@constructorParams) {
            my $constructor = IDLOperation->new();
            $constructor->name("Constructor");
            $constructor->extendedAttributes($extendedAttributeList);
            $constructor->arguments($param);
            push(@{$interface->constructors}, $constructor);
        }
        delete $extendedAttributeList->{"Constructors"};
        $extendedAttributeList->{"Constructor"} = "VALUE_IS_MISSING";
    } elsif (defined $extendedAttributeList->{"NamedConstructor"}) {
        my $newDataNode = IDLOperation->new();
        $newDataNode->name("NamedConstructor");
        $newDataNode->extendedAttributes($extendedAttributeList);
        my %attributes = %{$extendedAttributeList->{"NamedConstructor"}};
        my @attributeKeys = keys (%attributes);
        my $constructorName = $attributeKeys[0];
        push(@{$newDataNode->arguments}, @{$attributes{$constructorName}});
        $extendedAttributeList->{"NamedConstructor"} = $constructorName;
        push(@{$interface->constructors}, $newDataNode);
    }
    if (defined $extendedAttributeList->{"CustomConstructors"}) {
        my @customConstructorParams = @{$extendedAttributeList->{"CustomConstructors"}};
        my $index = (@customConstructorParams == 1) ? 0 : 1;
        foreach my $param (@customConstructorParams) {
            my $customConstructor = IDLOperation->new();
            $customConstructor->name("CustomConstructor");
            $customConstructor->extendedAttributes($extendedAttributeList);
            $customConstructor->arguments($param);
            push(@{$interface->customConstructors}, $customConstructor);
        }
        delete $extendedAttributeList->{"CustomConstructors"};
        $extendedAttributeList->{"CustomConstructor"} = "VALUE_IS_MISSING";
    }
    
    $interface->extendedAttributes($extendedAttributeList);
}

1;

