"""
The MIT License (MIT)
Copyright © 2018 Jean-Christophe Bos & HC² (www.hc2.fr)
"""

import re

class MicroWebTemplate :

    # ============================================================================
    # ===( Constants )============================================================
    # ============================================================================

	TOKEN_OPEN				= '{{'
	TOKEN_CLOSE				= '}}'
	TOKEN_OPEN_LEN			= len(TOKEN_OPEN)
	TOKEN_CLOSE_LEN			= len(TOKEN_CLOSE)

	INSTRUCTION_PYTHON		= 'py'
	INSTRUCTION_IF			= 'if'
	INSTRUCTION_ELIF		= 'elif'
	INSTRUCTION_ELSE		= 'else'
	INSTRUCTION_FOR			= 'for'
	INSTRUCTION_END			= 'end'
	INSTRUCTION_INCLUDE		= 'include'

    # ============================================================================
    # ===( Constructor )==========================================================
    # ============================================================================

	def __init__(self, code, escapeStrFunc=None, filepath='') :
		self._code   		= code
		self._escapeStrFunc	= escapeStrFunc
		self._filepath		= filepath
		self._pos    		= 0
		self._endPos 		= len(code)-1
		self._line   		= 1
		self._reIdentifier	= re.compile(r'[a-zA-Z_][a-zA-Z0-9_]*$')
		self._pyGlobalVars	= { }
		self._pyLocalVars	= { }
		self._rendered		= ''
		self._instructions	= {
			MicroWebTemplate.INSTRUCTION_PYTHON : self._processInstructionPYTHON,
			MicroWebTemplate.INSTRUCTION_IF 	: self._processInstructionIF,
			MicroWebTemplate.INSTRUCTION_ELIF 	: self._processInstructionELIF,
			MicroWebTemplate.INSTRUCTION_ELSE 	: self._processInstructionELSE,
			MicroWebTemplate.INSTRUCTION_FOR 	: self._processInstructionFOR,
			MicroWebTemplate.INSTRUCTION_END	: self._processInstructionEND,
			MicroWebTemplate.INSTRUCTION_INCLUDE: self._processInstructionINCLUDE,
		}

    # ============================================================================
    # ===( Functions )============================================================
    # ============================================================================

	def Validate(self) :
		try :
			self._parseCode(execute=False)
			return None
		except Exception as ex :
			return str(ex)

	# ----------------------------------------------------------------------------

	def Execute(self) :
		try :
			self._parseCode(execute=True)
			return self._rendered
		except Exception as ex :
			raise Exception(str(ex))

    # ============================================================================
    # ===( Utils  )===============================================================
    # ============================================================================
	
	def _parseCode(self, execute) :
		self._pyGlobalVars = { }
		self._pyLocalVars  = { }
		self._rendered	   = ''
		newTokenToProcess  = self._parseBloc(execute)
		if newTokenToProcess is not None :
			raise Exception( '"%s" instruction is not valid here (line %s)'
							 % (newTokenToProcess, self._line) )

    # ----------------------------------------------------------------------------

	def _parseBloc(self, execute) :
		while self._pos <= self._endPos :
			c = self._code[self._pos]
			if c == MicroWebTemplate.TOKEN_OPEN[0] and \
			    self._code[ self._pos : self._pos + MicroWebTemplate.TOKEN_OPEN_LEN ] == MicroWebTemplate.TOKEN_OPEN :
				self._pos    += MicroWebTemplate.TOKEN_OPEN_LEN
				tokenContent  = ''
				x 			  = self._pos
				while True :
					if x > self._endPos :
						raise Exception("%s is missing (line %s)" % (MicroWebTemplate.TOKEN_CLOSE, self._line))
					c = self._code[x]
					if c == MicroWebTemplate.TOKEN_CLOSE[0] and \
					   self._code[ x : x + MicroWebTemplate.TOKEN_CLOSE_LEN ] == MicroWebTemplate.TOKEN_CLOSE :
					   self._pos = x + MicroWebTemplate.TOKEN_CLOSE_LEN
					   break
					elif c == '\n' :
					 	self._line += 1
					tokenContent += c
					x 			 += 1
				newTokenToProcess = self._processToken(tokenContent, execute)
				if newTokenToProcess is not None :
					return newTokenToProcess
				continue
			elif c == '\n' :
				self._line += 1
			if execute :
				self._rendered += c
			self._pos += 1
		return None

	# ----------------------------------------------------------------------------

	def _processToken(self, tokenContent, execute) :
		tokenContent = tokenContent.strip()
		parts 		 = tokenContent.split(' ', 1)
		instructName = parts[0].strip()
		instructBody = parts[1].strip() if len(parts) > 1 else None
		if len(instructName) == 0 :
			raise Exception( '"%s %s" : instruction is missing (line %s)'
							 % (MicroWebTemplate.TOKEN_OPEN, MicroWebTemplate.TOKEN_CLOSE, self._line) )
		newTokenToProcess = None
		if instructName in self._instructions :
			newTokenToProcess = self._instructions[instructName](instructBody, execute)
		elif execute :
			try :
				s = str( eval( tokenContent,
							   self._pyGlobalVars,
							   self._pyLocalVars ) )
				if (self._escapeStrFunc is not None) :
					self._rendered += self._escapeStrFunc(s)
				else :
					self._rendered += s
			except Exception as ex :
				raise Exception('%s (line %s)' % (str(ex), self._line))
		return newTokenToProcess

	# ----------------------------------------------------------------------------

	def _processInstructionPYTHON(self, instructionBody, execute) :
		if instructionBody is not None :
			raise Exception( 'Instruction "%s" is invalid (line %s)'
							 % (MicroWebTemplate.INSTRUCTION_PYTHON, self._line) )
		pyCode = ''
		while True :
			if self._pos > self._endPos :
				raise Exception( '"%s" instruction is missing (line %s)'
								 % (MicroWebTemplate.INSTRUCTION_END, self._line) )
			c = self._code[self._pos]
			if c == MicroWebTemplate.TOKEN_OPEN[0] and \
			   self._code[ self._pos : self._pos + MicroWebTemplate.TOKEN_OPEN_LEN ] == MicroWebTemplate.TOKEN_OPEN :
				self._pos    += MicroWebTemplate.TOKEN_OPEN_LEN
				tokenContent  = ''
				x 			  = self._pos
				while True :
					if x > self._endPos :
						raise Exception("%s is missing (line %s)" % (MicroWebTemplate.TOKEN_CLOSE, self._line))
					c = self._code[x]
					if c == MicroWebTemplate.TOKEN_CLOSE[0] and \
					   self._code[ x : x + MicroWebTemplate.TOKEN_CLOSE_LEN ] == MicroWebTemplate.TOKEN_CLOSE :
					   self._pos = x + MicroWebTemplate.TOKEN_CLOSE_LEN
					   break
					elif c == '\n' :
					 	self._line += 1
					tokenContent += c
					x 			 += 1
				tokenContent = tokenContent.strip()
				if tokenContent == MicroWebTemplate.INSTRUCTION_END :
					break
				raise Exception( '"%s" is a bad instruction in a python bloc (line %s)'
								 % (tokenContent, self._line) )				
			elif c == '\n' :
				self._line += 1
			if execute :
				pyCode += c
			self._pos += 1
		if execute :
			lines  = pyCode.split('\n')
			indent = '' 
			for line in lines :
				if len(line.strip()) > 0 :
					for c in line :
						if c == ' ' or c == '\t' :
							indent += c
						else :
							break
					break
			pyCode = ''
			for line in lines :
				if line.find(indent) == 0 :
					line = line[len(indent):]
				pyCode += line + '\n'
			try :
				exec(pyCode, self._pyGlobalVars, self._pyLocalVars)
			except Exception as ex :
				raise Exception('%s (line %s)' % (str(ex), self._line))
		return None

	# ----------------------------------------------------------------------------

	def _processInstructionIF(self, instructionBody, execute) :
		if instructionBody is not None :
			if execute :
				try :
					result = eval(instructionBody, self._pyGlobalVars, self._pyLocalVars)
					if not isinstance(result, bool) :
						raise Exception('"%s" is not a boolean expression (line %s)' % (instructionBody, self._line))
				except Exception as ex :
					raise Exception('%s (line %s)' % (str(ex), self._line))
			else :
				result = False
			newTokenToProcess = self._parseBloc(execute and result)
			if newTokenToProcess is not None :
				if newTokenToProcess == MicroWebTemplate.INSTRUCTION_END :
					return None
				elif newTokenToProcess == MicroWebTemplate.INSTRUCTION_ELSE :
					newTokenToProcess = self._parseBloc(execute and not result)
					if newTokenToProcess is not None :
						if newTokenToProcess == MicroWebTemplate.INSTRUCTION_END :
							return None
						raise Exception( '"%s" instruction waited (line %s)'
										 % (MicroWebTemplate.INSTRUCTION_END, self._line) )
					raise Exception( '"%s" instruction is missing (line %s)'
									 % (MicroWebTemplate.INSTRUCTION_END, self._line) )
				elif newTokenToProcess == MicroWebTemplate.INSTRUCTION_ELIF :
					self._processInstructionIF(self._elifInstructionBody, execute and not result)
					return None
				raise Exception( '"%s" instruction waited (line %s)'
								 % (MicroWebTemplate.INSTRUCTION_END, self._line) )
			raise Exception( '"%s" instruction is missing (line %s)'
							 % (MicroWebTemplate.INSTRUCTION_END, self._line) )
		raise Exception( '"%s" alone is an incomplete syntax (line %s)'
						 % (MicroWebTemplate.INSTRUCTION_IF, self._line) )

	# ----------------------------------------------------------------------------

	def _processInstructionELIF(self, instructionBody, execute) :
		if instructionBody is None :
			raise Exception( '"%s" alone is an incomplete syntax (line %s)'
							 % (MicroWebTemplate.INSTRUCTION_ELIF, self._line) )
		self._elifInstructionBody = instructionBody
		return MicroWebTemplate.INSTRUCTION_ELIF

	# ----------------------------------------------------------------------------

	def _processInstructionELSE(self, instructionBody, execute) :
		if instructionBody is not None :
			raise Exception( 'Instruction "%s" is invalid (line %s)'
							 % (MicroWebTemplate.INSTRUCTION_ELSE, self._line) )
		return MicroWebTemplate.INSTRUCTION_ELSE

	# ----------------------------------------------------------------------------

	def _processInstructionFOR(self, instructionBody, execute) :
		if instructionBody is not None :
			parts 	   = instructionBody.split(' ', 1)
			identifier = parts[0].strip()
			if self._reIdentifier.match(identifier) is not None and len(parts) > 1 :
				parts = parts[1].strip().split(' ', 1)
				if parts[0] == 'in' and len(parts) > 1 :
					expression  	   = parts[1].strip()
					newTokenToProcess  = None
					beforePos   	   = self._pos
					if execute :
						try :
							result = eval(expression, self._pyGlobalVars, self._pyLocalVars)
						except :
							raise Exception('%s (line %s)' % (str(expression), self._line))
					if execute and len(result) > 0 :
						for x in result :
							self._pyLocalVars[identifier] = x
							self._pos  					  = beforePos
							newTokenToProcess	  		  = self._parseBloc(True)
							if newTokenToProcess != MicroWebTemplate.INSTRUCTION_END :
								break
					else :
						newTokenToProcess = self._parseBloc(False)
					if newTokenToProcess is not None :
						if newTokenToProcess == MicroWebTemplate.INSTRUCTION_END :
							return None
						raise Exception( '"%s" instruction waited (line %s)'
										 % (MicroWebTemplate.INSTRUCTION_END, self._line) )
					raise Exception( '"%s" instruction is missing (line %s)'
										 % (MicroWebTemplate.INSTRUCTION_END, self._line) )
			raise Exception( '"%s %s" is an invalid syntax'
							 % (MicroWebTemplate.INSTRUCTION_FOR, instructionBody) )
		raise Exception( '"%s" alone is an incomplete syntax (line %s)'
						 % (MicroWebTemplate.INSTRUCTION_FOR, self._line) )

	# ----------------------------------------------------------------------------

	def _processInstructionEND(self, instructionBody, execute) :
		if instructionBody is not None :
			raise Exception( 'Instruction "%s" is invalid (line %s)'
							 % (MicroWebTemplate.INSTRUCTION_END, self._line) )
		return MicroWebTemplate.INSTRUCTION_END

	# ----------------------------------------------------------------------------

	def _processInstructionINCLUDE(self, instructionBody, execute) :
		if not instructionBody :
			raise Exception( '"%s" alone is an incomplete syntax (line %s)' % (MicroWebTemplate.INSTRUCTION_INCLUDE, self._line) )
		filename = instructionBody.replace('"','').replace("'",'').strip()
		idx = self._filepath.rindex('/')
		if idx >= 0 :
			filename = self._filepath[:idx+1] + filename
		with open(filename, 'r') as file :
			includeCode = file.read()

			self._code = self._code[:self._pos] + includeCode + self._code[self._pos:]
			self._endPos += len(includeCode)

    # ============================================================================
    # ============================================================================
    # ============================================================================
